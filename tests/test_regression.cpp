// CXL Fabric regression tests: selection result lifetime, failover failure
// domains, reservation release, restart accounting, region updates, and
// device-boot fencing.
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <cstdint>
#include <iostream>
#include <string>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cout << "FAIL: " << msg << "\n"; ++failures; } } while (0)

using namespace cxl_fabric;

namespace {

constexpr std::uint64_t GiB = 1024ull * 1024 * 1024;

AuthorityContext ctx_of(const Fabric& f, const std::string& boot = "") {
  AuthorityContext c;
  c.coordinator_epoch = f.epoch();
  c.policy_generation = f.policy_generation();
  c.consumer_generation = ConsumerGeneration(1);
  if (!boot.empty()) c.worker_boot_id = WorkerBootId(boot);
  return c;
}

// One 16 GiB device in one pool with one 16 GiB region.
void single(Fabric& f) {
  AuthorityContext c = ctx_of(f);
  f.register_device(synthetic::make_device(DeviceId("d1"), DeviceGeneration(1), DeviceBootId("b1"), HostId("h"),
                    16 * GiB, VolatilityKind::VOLATILE, SharingMode::DEDICATED, LocalityClass::SAME_HOST,
                    180, 32000, 1, "fd"), c);
  f.create_pool(synthetic::make_pool(PoolId("p"), PoolGeneration(1),
                {{DeviceId("d1"), DeviceGeneration(1), 16 * GiB, true, true, "fd", LocalityClass::SAME_HOST}}), c);
  f.create_region(synthetic::make_region(RegionId("r"), RegionGeneration(1), PoolId("p"), {DeviceId("d1")},
                  16 * GiB, LocalityClass::SAME_HOST, TierClass::CXL_NEAR, RegionState::ONLINE), c);
}

AdmissionRequest req_bytes(std::uint64_t bytes) {
  AdmissionRequest r; r.bytes = bytes; r.consumer = ConsumerId("svc"); return r;
}

const CxlPool& pool_of(const StateSnapshot& s) { return s.pools.at(PoolId("p")); }
const CxlRegion& region_of(const StateSnapshot& s) { return s.regions.at(RegionId("r")); }

}  // namespace

int main() {
  // Records referenced by a selection or failover result stay valid after the
  // call returns (they used to point into a snapshot destroyed on return).
  {
    Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
    single(f);
    SelectionOutcome so = f.select(req_bytes(GiB));
    CHECK(so.ok, "selection ok");
    if (so.ok) {
      CHECK(so.chosen.device_record != nullptr && so.chosen.device_record->id == so.chosen.device &&
            so.chosen.region_record != nullptr && so.chosen.region_record->id == so.chosen.region &&
            so.chosen.pool_record != nullptr && so.chosen.pool_record->id == so.chosen.pool,
            "selected candidate records usable after select() returns");
    }
  }

  // Failover classifies a replacement by the pool's failure-domain membership.
  {
    Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
    AuthorityContext c = ctx_of(f);
    for (const char* id : {"a1", "a2"}) {
      f.register_device(synthetic::make_device(DeviceId(id), DeviceGeneration(1), DeviceBootId(std::string(id) + "b"),
                        HostId("h"), 16 * GiB, VolatilityKind::VOLATILE, SharingMode::DEDICATED,
                        LocalityClass::SAME_HOST, 180, 32000, 1, std::string("notes-") + id), c);
    }
    f.create_pool(synthetic::make_pool(PoolId("p"), PoolGeneration(1), {
        {DeviceId("a1"), DeviceGeneration(1), 16 * GiB, true, true, "rack-1", LocalityClass::SAME_HOST},
        {DeviceId("a2"), DeviceGeneration(1), 16 * GiB, true, true, "rack-1", LocalityClass::SAME_HOST}}), c);
    f.create_region(synthetic::make_region(RegionId("r"), RegionGeneration(1), PoolId("p"),
                    {DeviceId("a1"), DeviceId("a2")},
                    32 * GiB, LocalityClass::SAME_HOST, TierClass::CXL_NEAR, RegionState::ONLINE), c);
    Fabric::FailoverPlan fp = f.plan_failover(PoolId("p"), DeviceId("a1"), req_bytes(GiB), c);
    CHECK(fp.replacement.has_value(), "failover found a replacement");
    CHECK(fp.outcome == FailoverOutcome::DEGRADED, "same-failure-domain replacement reported as degraded");
    if (fp.replacement) {
      CHECK(fp.replacement->device_record != nullptr && fp.replacement->device_record->id == DeviceId("a2"),
            "failover replacement record usable after plan_failover() returns");
    }
  }

  // Releasing an uncommitted reservation releases reserved bytes, not another
  // reservation's committed bytes.
  {
    Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
    single(f);
    AuthorityContext c = ctx_of(f);
    ReserveOutcome r1 = f.reserve(req_bytes(GiB), c);
    CHECK(r1.ok && f.commit(r1.reservation.id, c).ok, "first reservation committed");
    ReserveOutcome r2 = f.reserve(req_bytes(2 * GiB), c);
    CHECK(r2.ok, "second reservation reserved");
    CHECK(f.release(r2.reservation.id, c).ok, "uncommitted reservation released");
    StateSnapshot s = f.snapshot();
    CHECK(pool_of(s).committed_bytes == GiB && pool_of(s).reserved_bytes == 0,
          "pool keeps the committed reservation's bytes committed");
    CHECK(region_of(s).committed_bytes == GiB && region_of(s).reserved_bytes == 0,
          "region keeps the committed reservation's bytes committed");
  }

  // A reservation is released only by the worker incarnation that holds it.
  {
    Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
    single(f);
    AuthorityContext a = ctx_of(f, "boot-a");
    ReserveOutcome r = f.reserve(req_bytes(GiB), a);
    CHECK(r.ok && f.commit(r.reservation.id, a).ok, "reservation committed by worker A");
    ReserveOutcome other = f.release(r.reservation.id, ctx_of(f, "boot-b"));
    CHECK(!other.ok && other.reason == DecisionReason::RESERVATION_STALE, "another worker's release rejected");
    CHECK(pool_of(f.snapshot()).committed_bytes == GiB, "committed bytes untouched by the rejected release");
    CHECK(f.release(r.reservation.id, a).ok, "owning worker releases");
  }

  // A coordinator restart drops reservations, so the capacity they held is
  // free again instead of leaking.
  {
    Fabric f1(CoordinatorEpoch(1), PolicyGeneration(1));
    single(f1);
    AuthorityContext c1 = ctx_of(f1);
    ReserveOutcome r = f1.reserve(req_bytes(8 * GiB), c1);
    CHECK(r.ok && f1.commit(r.reservation.id, c1).ok, "8 GiB committed before restart");
    PersistedState st;
    CHECK(f1.export_persisted(st), "export");
    Fabric f2(CoordinatorEpoch(2), PolicyGeneration(1));
    f2.import_persisted(st);
    AuthorityContext c2 = ctx_of(f2);
    CHECK(f2.mark_device_online(DeviceId("d1"), c2) == DecisionReason::OK, "device revalidated");
    CHECK(f2.revalidate_region(RegionId("r"), c2) == DecisionReason::OK, "region revalidated");
    CHECK(f2.revalidate_pool(PoolId("p"), c2) == DecisionReason::OK, "pool revalidated");
    StateSnapshot s = f2.snapshot();
    CHECK(pool_of(s).committed_bytes == 0 && pool_of(s).reserved_bytes == 0, "no capacity held by dropped reservations");
    CHECK(f2.reserve(req_bytes(16 * GiB), c2).ok, "full capacity reservable after restart");
  }

  // Updating a region never rewrites the accounting its reservations own.
  {
    Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
    single(f);
    AuthorityContext c = ctx_of(f);
    ReserveOutcome r = f.reserve(req_bytes(4 * GiB), c);
    CHECK(r.ok, "4 GiB reserved");
    CHECK(f.update_region(synthetic::make_region(RegionId("r"), RegionGeneration(1), PoolId("p"), {DeviceId("d1")},
                          16 * GiB, LocalityClass::SAME_HOST, TierClass::CXL_NEAR, RegionState::ONLINE), c) ==
          DecisionReason::OK, "region updated");
    CHECK(region_of(f.snapshot()).reserved_bytes == 4 * GiB, "region update keeps reserved bytes");
    CHECK(f.commit(r.reservation.id, c).ok, "commit after the update");
    StateSnapshot s = f.snapshot();
    CHECK(region_of(s).reserved_bytes == 0 && region_of(s).committed_bytes == 4 * GiB,
          "region accounting exact after commit");
  }

  // A reservation made before a device reboot cannot commit afterwards.
  {
    Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
    single(f);
    AuthorityContext c = ctx_of(f);
    ReserveOutcome r = f.reserve(req_bytes(GiB), c);
    CHECK(r.ok, "reserved on boot b1");
    CHECK(f.register_device(synthetic::make_device(DeviceId("d1"), DeviceGeneration(1), DeviceBootId("b1-reboot"),
                            HostId("h"), 16 * GiB, VolatilityKind::VOLATILE, SharingMode::DEDICATED,
                            LocalityClass::SAME_HOST, 180, 32000, 1, "fd"), c) == DecisionReason::OK,
          "device re-registered after a reboot");
    ReserveOutcome co = f.commit(r.reservation.id, c);
    CHECK(!co.ok && co.reason == DecisionReason::DEVICE_STALE, "commit across a device reboot rejected");
  }

  if (failures) { std::cout << "test_regression FAILURES=" << failures << "\n"; return 1; }
  std::cout << "PASS: test_regression\n";
  return 0;
}
