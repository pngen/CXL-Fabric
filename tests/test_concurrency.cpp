
// Real concurrency stress: many threads race reserve/commit/release against a
// single Fabric. The Fabric uses narrow locks and immutable snapshots for
// reads, so no thread ever blocks on socket/backend/persistence work. The
// accounting must close exactly after all threads join.
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

int main() {
  using namespace cxl_fabric;
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  HostId host("host-conc");
  CxlDevice d = synthetic::make_device(DeviceId("dev-1"), DeviceGeneration(1), DeviceBootId("b1"), host,
      256ull*1024*1024*1024, VolatilityKind::VOLATILE, SharingMode::SHARED, LocalityClass::SAME_HOST, 180, 32000, 1, "fd");
  f.register_device(d, ctx);
  f.create_pool(synthetic::make_pool(PoolId("p-1"), PoolGeneration(1),
      {{DeviceId("dev-1"), DeviceGeneration(1), 256ull*1024*1024*1024, true,true,"fd",LocalityClass::SAME_HOST}}), ctx);
  f.create_region(synthetic::make_region(RegionId("r1"), RegionGeneration(1), PoolId("p-1"), {DeviceId("dev-1")},
      256ull*1024*1024*1024, LocalityClass::SAME_HOST, TierClass::CXL_NEAR, RegionState::ONLINE), ctx);

  std::atomic<int> reserves{0}, commits{0}, releases{0}, fails{0};
  const int threads = 8, iters = 2000;
  std::vector<std::thread> ts;
  for (int t = 0; t < threads; ++t) {
    // Capture t by value: the outer loop variable's scope ends before the
    // worker threads join, so capturing it by reference ([&]) is a genuine data
    // race and a stack-use-after-scope (caught by ASan). By-value capture also
    // gives each thread the distinct consumer id the test intends.
    ts.emplace_back([&, t]{
      AuthorityContext c; c.coordinator_epoch=f.epoch(); c.policy_generation=f.policy_generation(); c.consumer_generation=ConsumerGeneration(1);
      for (int i = 0; i < iters; ++i) {
        AdmissionRequest req; req.bytes = 1ull<<30; req.consumer = ConsumerId("c"+std::to_string(t));
        ReserveOutcome ro = f.reserve(req, c);
        if (!ro.ok) { ++fails; continue; }
        ++reserves;
        ReserveOutcome co = f.commit(ro.reservation.id, c);
        if (co.ok) ++commits;
        ReserveOutcome rr = f.release(ro.reservation.id, c);
        if (rr.ok) ++releases;
      }
    });
  }
  for (auto& th : ts) th.join();

  AuditReport ar = f.audit();
  std::cout << "reserves=" << reserves << " commits=" << commits << " releases=" << releases
            << " fails=" << fails << " audit_ok=" << (ar.ok ? "true" : "false") << "\n";
  if (!ar.ok) {
    for (auto& e : ar.errors) std::cout << "  " << e << "\n";
    return 1;
  }
  // No reservations should be left; accounting closes exactly.
  if (ar.global_ledger.committed != 0 || ar.global_ledger.reserved != 0) {
    std::cout << "FAIL: leftover accounting\n"; return 1;
  }
  std::cout << "PASS: test_concurrency\n";
  return 0;
}
