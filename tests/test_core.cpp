
// CXL Fabric core tests
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/accounting.hpp"
#include "cxl_fabric/core/enum_strings.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include "cxl_fabric/core/identity.hpp"
#include "cxl_fabric/core/lifecycle.hpp"
#include <cstdint>
#include <iostream>
#include <string>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cout << "FAIL: " << msg << "\n"; ++failures; } } while (0)

using namespace cxl_fabric;

static void setup(Fabric& f) {
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  HostId host("host-test");
  for (auto& d : synthetic::baseline_devices(host)) f.register_device(d, ctx);
  f.create_pool(synthetic::make_pool(PoolId("p-1"), PoolGeneration(1), {
      {DeviceId("dev-1"), DeviceGeneration(1), 16ull*1024*1024*1024, true,true,"fd-a",LocalityClass::SAME_HOST},
      {DeviceId("dev-2"), DeviceGeneration(1), 64ull*1024*1024*1024, true,true,"fd-b",LocalityClass::REMOTE_NUMA_DOMAIN} }), ctx);
  f.create_region(synthetic::make_region(RegionId("r1"), RegionGeneration(1), PoolId("p-1"), {DeviceId("dev-1")},
      16ull*1024*1024*1024, LocalityClass::SAME_HOST, TierClass::CXL_NEAR, RegionState::ONLINE), ctx);
  f.create_region(synthetic::make_region(RegionId("r2"), RegionGeneration(1), PoolId("p-1"), {DeviceId("dev-2")},
      64ull*1024*1024*1024, LocalityClass::REMOTE_NUMA_DOMAIN, TierClass::CXL_REMOTE, RegionState::ONLINE), ctx);
}

int main() {
  // IDs
  DeviceId a("a"); DeviceId b("b");
  CHECK(a.empty() == false && a == DeviceId("a") && a != b, "id equality");
  CHECK(DeviceId().empty(), "default id empty");
  // Generations
  Generation<DeviceGenTag> g(1);
  CHECK(!g.empty() && g.next().value() == 2, "generation next");
  CHECK(Generation<DeviceGenTag>(UINT64_MAX - 1).next().empty(), "generation max next invalid");
  CHECK(Generation<DeviceGenTag>() < g, "generation ordering");
  // Overflow-safe arithmetic
  std::uint64_t out;
  CHECK(!add_checked(UINT64_MAX, 1, out), "add overflow rejected");
  CHECK(add_checked(5, 3, out) && out == 8, "add ok");
  CHECK(!sub_checked(3, 5, out), "sub underflow rejected");
  CHECK(!mul_checked(UINT64_MAX, 2, out), "mul overflow rejected");
  // CapacityLine
  CapacityLine line;
  line.total = (1ull << 40); line.online = (1ull << 40);
  std::string err;
  CHECK(line.audit(err), "line audit ok initially");
  CHECK(!line.reserve(0), "reserve zero rejected");
  CHECK(line.reserve(1ull << 30), "reserve 1GiB");
  CHECK(!line.commit(1ull << 31), "commit beyond reserved rejected");
  CHECK(line.commit(1ull << 30), "commit reserved ok");
  CHECK(line.reserve(2ull << 30), "reserve 2GiB");
  CHECK(!line.release(1ull << 40), "release beyond outstanding rejected");
  CHECK(line.release(2ull << 30), "release 2GiB total");  // releases 1GiB committed + 1GiB reserved
  // free should be total - 1GiB committed
  CHECK(line.free() == (1ull << 40) - (1ull << 30), "free accounts exactly");
  CHECK(line.audit(err), "line audit ok after ops");
  // Overflow: bad facade
  CapacityLine big; big.total = UINT64_MAX; big.online = UINT64_MAX;
  CHECK(big.free() == UINT64_MAX, "free large");
  // Enum round-trip
  auto p = from_string("SYNTHETIC", (Provenance*)nullptr);
  CHECK(p && *p == Provenance::SYNTHETIC && std::string(to_string(Provenance::REAL)) == "REAL", "provenance round-trip");
  CHECK(!from_string("NOPE", (Provenance*)nullptr), "unknown provenance rejected");
  auto ds = from_string("ONLINE", (DeviceState*)nullptr);
  CHECK(ds && *ds == DeviceState::ONLINE, "device state round-trip");
  // Lifecycle transitions
  CHECK(valid_transition(DeviceState::ONLINE, DeviceState::DRAINING), "online->draining");
  CHECK(!valid_transition(DeviceState::RETIRED, DeviceState::ONLINE), "retired->online invalid");
  CHECK(valid_transition(DeviceState::REVALIDATION_REQUIRED, DeviceState::ONLINE), "revalidation->online");
  // Fabric end-to-end
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  setup(f);
  // Deterministic selection
  AdmissionRequest req; req.bytes = 4ull*1024*1024*1024; req.consumer=ConsumerId("svc");
  SelectionOutcome so1 = f.select(req);
  SelectionOutcome so2 = f.select(req);
  CHECK(so1.ok && so2.ok, "selection ok");
  CHECK(so1.chosen.region == so2.chosen.region && so1.chosen.device == so2.chosen.device, "selection deterministic");
  // Reserve/commit/release, accounting exact
  ReserveOutcome ro = f.reserve(req, ctx);
  CHECK(ro.ok, "reserve ok");
  CHECK(ro.reservation.state == ResumeState::RESERVED, "reserved state");
  ReserveOutcome co = f.commit(ro.reservation.id, ctx);
  CHECK(co.ok, "commit ok");
  CHECK(co.reservation.state == ResumeState::COMMITTED, "committed state");
  AuditReport ar = f.audit();
  CHECK(ar.ok, "audit ok after commit");
  CHECK(ar.global_ledger.committed == (4ull*1024*1024*1024), "committed exact");
  ReserveOutcome rr = f.release(ro.reservation.id, ctx);
  CHECK(rr.ok, "release ok");
  CHECK(rr.reservation.state == ResumeState::RELEASED, "released state");
  CHECK(f.audit().global_ledger.committed == 0, "committed zero after release");
  // Double release rejected
  CHECK(!f.release(ro.reservation.id, ctx).ok, "double release rejected");
  // Stale epoch rejected
  AuthorityContext stale_ctx; stale_ctx.coordinator_epoch = CoordinatorEpoch(999); stale_ctx.policy_generation = PolicyGeneration(1);
  stale_ctx.consumer_generation = ConsumerGeneration(1);
  // staling the epoch in reserve is rejected?
  {
    Fabric f2(CoordinatorEpoch(5), PolicyGeneration(1));
    CHECK(f2.reserve(req, stale_ctx).reason == DecisionReason::RESERVATION_STALE, "stale epoch reserve rejected");
  }
  // Conflicting duplicate device id at same generation
  {
    Fabric f3(CoordinatorEpoch(1), PolicyGeneration(1));
    AuthorityContext c; c.coordinator_epoch=f3.epoch(); c.policy_generation=f3.policy_generation(); c.consumer_generation=ConsumerGeneration(1);
    CxlDevice d = synthetic::make_device(DeviceId("d"), DeviceGeneration(1), DeviceBootId("b"), HostId("h"), 1ull<<30, VolatilityKind::VOLATILE, SharingMode::DEDICATED, LocalityClass::SAME_HOST, 200,16000,1,"fd");
    f3.register_device(d, c);
    CxlDevice d2 = d; d2.vendor_device_id = "DIFFERENT-VENDOR";
    CHECK(f3.register_device(d2, c) == DecisionReason::CONFLICTING_FACTS, "conflicting vendor rejected");
    d2 = d; d2.generation = DeviceGeneration(2); d2.total_physical_bytes = 1ull<<40;
    CHECK(f3.register_device(d2, c) == DecisionReason::OK, "new generation supersedes");
  }
  // REVALIDATION_REQUIRED grants no new authority
  {
    Fabric f4(CoordinatorEpoch(1), PolicyGeneration(1));
    AuthorityContext c; c.coordinator_epoch=f4.epoch(); c.policy_generation=f4.policy_generation(); c.consumer_generation=ConsumerGeneration(1);
    setup(f4);
    f4.mark_device_revalidation(DeviceId("dev-1"), c);
    f4.mark_device_revalidation(DeviceId("dev-2"), c);
    AdmissionRequest r; r.bytes=1ull<<30; r.consumer=ConsumerId("svc");
    SelectionOutcome so = f4.select(r);
    CHECK(!so.ok, "revalidation-required devices never grant authority");
  }
  // Pool generation change fends an old reservation commit
  {
    Fabric f5(CoordinatorEpoch(1), PolicyGeneration(1));
    AuthorityContext c; c.coordinator_epoch=f5.epoch(); c.policy_generation=f5.policy_generation(); c.consumer_generation=ConsumerGeneration(1);
    setup(f5);
    AdmissionRequest r; r.bytes=1ull<<30; r.consumer=ConsumerId("svc");
    ReserveOutcome ro5 = f5.reserve(r, c);
    CHECK(ro5.ok, "reserve ok");
    PoolMember m; m.device=DeviceId("dev-3"); m.device_generation=DeviceGeneration(1); m.nominal_bytes=8ull*1024*1024*1024; m.online=true; m.health_ok=true; m.failure_domain="fd-c";
    f5.register_device(synthetic::make_device(DeviceId("dev-3"), DeviceGeneration(1), DeviceBootId("b3"), HostId("host-test"), 8ull*1024*1024*1024, VolatilityKind::VOLATILE, SharingMode::DEDICATED, LocalityClass::SAME_HOST, 200,16000,1,"fd-c"), c);
    f5.add_pool_member(PoolId("p-1"), m, c);
    CHECK(!f5.commit(ro5.reservation.id, c).ok, "old pool-gen commit fenced");
  }
  if (failures) { std::cout << "test_core FAILURES=" << failures << "\n"; return 1; }
  std::cout << "PASS: test_core\n";
  return 0;
}