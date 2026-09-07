
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <iostream>
int main(){
  using namespace cxl_fabric;
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  HostId host("host-fs");
  CxlDevice d1 = synthetic::make_device(DeviceId("d1"), DeviceGeneration(1), DeviceBootId("b1"), host,
      16ull*1024*1024*1024, VolatilityKind::VOLATILE, SharingMode::DEDICATED, LocalityClass::SAME_HOST, 200,16000,1,"fd-a");
  CxlDevice d2 = synthetic::make_device(DeviceId("d2"), DeviceGeneration(1), DeviceBootId("b2"), host,
      24ull*1024*1024*1024, VolatilityKind::VOLATILE, SharingMode::DEDICATED, LocalityClass::SAME_CXL_ROOT, 300,20000,2,"fd-b");
  f.register_device(d1, ctx); f.register_device(d2, ctx);
  f.create_pool(synthetic::make_pool(PoolId("p-1"), PoolGeneration(1), {
      {DeviceId("d1"), DeviceGeneration(1), 16ull*1024*1024*1024, true,true,"fd-a",LocalityClass::SAME_HOST},
      {DeviceId("d2"), DeviceGeneration(1), 24ull*1024*1024*1024, true,true,"fd-b",LocalityClass::SAME_CXL_ROOT} }), ctx);
  f.create_region(synthetic::make_region(RegionId("r1"), RegionGeneration(1), PoolId("p-1"), {DeviceId("d1")},
      16ull*1024*1024*1024, LocalityClass::SAME_HOST, TierClass::CXL_NEAR, RegionState::ONLINE), ctx);
  f.create_region(synthetic::make_region(RegionId("r2"), RegionGeneration(1), PoolId("p-1"), {DeviceId("d2")},
      24ull*1024*1024*1024, LocalityClass::SAME_CXL_ROOT, TierClass::CXL_CAPACITY, RegionState::ONLINE), ctx);
  AdmissionRequest req; req.bytes=1ull<<30; req.consumer=ConsumerId("svc");
  // Fail d1 (its region loses it): plan failover should prefer cross-domain d2 (fd-b).
  f.mark_device_revalidation(DeviceId("d1"), ctx);
  auto fp = f.plan_failover(PoolId("p-1"), DeviceId("d1"), req, ctx);
  std::cout << "outcome=" << to_string(fp.outcome) << " note=" << fp.note << "\n";
  if (fp.outcome != FailoverOutcome::FAILOVER && fp.outcome != FailoverOutcome::REJECT) { std::cout << "FAIL: unexpected failover\n"; return 1; }
  std::cout << "PASS: failover_selection\n";
  return 0;
}