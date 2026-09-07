
// Seeded randomized/property test. Invariants are re-checked after every
// mutation; the seed is printed if a check fails.
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace cxl_fabric;
static int failures = 0;
#define CHECK(c,m) do { if (!(c)) { std::cout << "FAIL(" << seed << "): " << m << "\n"; ++failures; } } while (0)

static std::uint32_t seed = 0x9E3779B9u;

int main() {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<std::uint64_t> siz(1ull << 20, 8ull << 30);
  std::uniform_int_distribution<int> op(0, 3);
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  HostId host("host-prop");
  for (auto& d : synthetic::baseline_devices(host)) f.register_device(d, ctx);
  f.create_pool(synthetic::make_pool(PoolId("p-1"), PoolGeneration(1), {
      {DeviceId("dev-1"), DeviceGeneration(1), 16ull*1024*1024*1024, true,true,"fd-a",LocalityClass::SAME_HOST},
      {DeviceId("dev-2"), DeviceGeneration(1), 64ull*1024*1024*1024, true,true,"fd-b",LocalityClass::REMOTE_NUMA_DOMAIN} }), ctx);
  f.create_region(synthetic::make_region(RegionId("r1"), RegionGeneration(1), PoolId("p-1"), {DeviceId("dev-1")},
      16ull*1024*1024*1024, LocalityClass::SAME_HOST, TierClass::CXL_NEAR, RegionState::ONLINE), ctx);
  f.create_region(synthetic::make_region(RegionId("r2"), RegionGeneration(1), PoolId("p-1"), {DeviceId("dev-2")},
      64ull*1024*1024*1024, LocalityClass::REMOTE_NUMA_DOMAIN, TierClass::CXL_REMOTE, RegionState::ONLINE), ctx);
  (void)ctx;

  struct Tracked { ReservationId id; ResumeState st; };
  std::vector<Tracked> tracked;
  const int N = 2000;
  for (int i = 0; i < N; ++i) {
    int o = op(rng);
    AuthorityContext c; c.coordinator_epoch=f.epoch(); c.policy_generation=f.policy_generation(); c.consumer_generation=ConsumerGeneration(1);
    if (o == 0) {
      AdmissionRequest req; req.bytes = siz(rng); req.consumer = ConsumerId("svc");
      ReserveOutcome ro = f.reserve(req, c);
      if (ro.ok) tracked.push_back({ro.reservation.id, ro.reservation.state});
    } else if (o == 1 && !tracked.empty()) {
      auto idx = rng() % tracked.size();
      if (tracked[idx].st == ResumeState::RESERVED) {
        ReserveOutcome co = f.commit(tracked[idx].id, c);
        if (co.ok) tracked[idx].st = co.reservation.state;
      }
    } else if (o == 2 && !tracked.empty()) {
      auto idx = rng() % tracked.size();
      if (tracked[idx].st == ResumeState::RESERVED || tracked[idx].st == ResumeState::COMMITTED) {
        ReserveOutcome rr = f.release(tracked[idx].id, c);
        if (rr.ok) { tracked[idx].st = ResumeState::RELEASED; tracked.erase(tracked.begin() + idx); }
      }
    } else {
      // Randomly create a transient reservation and release it after a device
      // revalidation cycle to exercise stale-evidence paths.
      AdmissionRequest req; req.bytes = (siz(rng) % (4ull << 30)); if (req.bytes == 0) req.bytes = 1ull<<20; req.consumer=ConsumerId("x");
      ReserveOutcome ro = f.reserve(req, c);
      if (ro.ok) { f.release(ro.reservation.id, c); }
      f.mark_device_revalidation(rng() % 2 ? DeviceId("dev-1") : DeviceId("dev-2"), c);
      f.mark_device_online(rng() % 2 ? DeviceId("dev-1") : DeviceId("dev-2"), c);
    }
    // Invariant: accounting closes exactly (no negative, free>=0, used<=online).
    AuditReport ar = f.audit();
    CHECK(ar.ok, "audit invariant after op");
  }
  if (failures) { std::cout << "test_property FAILURES=" << failures << " (seed=" << seed << ")\n"; return 1; }
  std::cout << "PASS: test_property (seed=" << seed << ")\n";
  return 0;
}