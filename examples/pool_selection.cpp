
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <iostream>

static void mk(cxl_fabric::Fabric& f, cxl_fabric::AuthorityContext& ctx) {
  using namespace cxl_fabric;
  HostId host("host-se");
  CxlDevice near = synthetic::make_device(DeviceId("near"), DeviceGeneration(1), DeviceBootId("b-near"), host,
      8ull*1024*1024*1024, VolatilityKind::VOLATILE, SharingMode::DEDICATED,
      LocalityClass::SAME_HOST, 180, 32000, 1, "fd-a");
  CxlDevice far = synthetic::make_device(DeviceId("far"), DeviceGeneration(1), DeviceBootId("b-far"), host,
      64ull*1024*1024*1024, VolatilityKind::VOLATILE, SharingMode::DEDICATED,
      LocalityClass::SWITCH_REMOTE, 500, 12000, 4, "fd-b");
  f.register_device(near, ctx);
  f.register_device(far, ctx);
  CxlPool pool = synthetic::make_pool(PoolId("p-1"), PoolGeneration(1), {
      {DeviceId("near"), DeviceGeneration(1), 8ull*1024*1024*1024, true, true, "fd-a", LocalityClass::SAME_HOST},
      {DeviceId("far"), DeviceGeneration(1), 64ull*1024*1024*1024, true, true, "fd-b", LocalityClass::SWITCH_REMOTE} });
  f.create_pool(pool, ctx);
  f.create_region(synthetic::make_region(RegionId("r-near"), RegionGeneration(1), PoolId("p-1"),
      {DeviceId("near")}, 8ull*1024*1024*1024, LocalityClass::SAME_HOST, TierClass::CXL_NEAR, RegionState::ONLINE), ctx);
  f.create_region(synthetic::make_region(RegionId("r-far"), RegionGeneration(1), PoolId("p-1"),
      {DeviceId("far")}, 64ull*1024*1024*1024, LocalityClass::SWITCH_REMOTE, TierClass::CXL_CAPACITY, RegionState::ONLINE), ctx);
}

int main() {
  using namespace cxl_fabric;
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  mk(f, ctx);
  AdmissionRequest req; req.bytes = 4ull*1024*1024*1024; req.consumer=ConsumerId("svc");
  SelectionOutcome so = f.select(req);
  if (!so.ok) { std::cout << "FAIL: no selection\n"; return 1; }
  std::cout << "selected " << so.chosen.region.str() << " (locality " << to_string(so.chosen.device_record->economics.locality) << ")\n";
  if (so.chosen.region != RegionId("r-near")) { std::cout << "FAIL: expected nearest region r-near\n"; return 1; }
  // Larger far region should win only when a locality requirement excludes near.
  AdmissionRequest req2 = req; req2.bytes = 24ull*1024*1024*1024;
  SelectionOutcome so2 = f.select(req2);
  if (!so2.ok) { std::cout << "FAIL: no selection for large\n"; return 1; }
  if (so2.chosen.region != RegionId("r-far")) { std::cout << "FAIL: expected far region for large request\n"; return 1; }
  std::cout << "PASS: pool_selection\n";
  return 0;
}