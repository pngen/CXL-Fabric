
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <iostream>

int main() {
  using namespace cxl_fabric;
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  HostId host("host-loc");
  CxlDevice d = synthetic::make_device(DeviceId("d1"), DeviceGeneration(1), DeviceBootId("b1"), host,
      16ull*1024*1024*1024, VolatilityKind::VOLATILE, SharingMode::DEDICATED,
      LocalityClass::REMOTE_NUMA_DOMAIN, 350, 20000, 2, "fd-x");
  f.register_device(d, ctx);
  f.create_pool(synthetic::make_pool(PoolId("p-1"), PoolGeneration(1),
      {{DeviceId("d1"), DeviceGeneration(1), 16ull*1024*1024*1024, true, true, "fd-x", LocalityClass::REMOTE_NUMA_DOMAIN}}), ctx);
  f.create_region(synthetic::make_region(RegionId("r1"), RegionGeneration(1), PoolId("p-1"),
      {DeviceId("d1")}, 16ull*1024*1024*1024, LocalityClass::REMOTE_NUMA_DOMAIN, TierClass::CXL_CAPACITY, RegionState::ONLINE), ctx);
  AdmissionRequest req; req.bytes=4ull*1024*1024*1024; req.consumer=ConsumerId("svc");
  req.minimum_locality = LocalityClass::SAME_NUMA_DOMAIN;
  SelectionOutcome so = f.select(req);
  if (so.ok) { std::cout << "FAIL: REMOTE_NUMA satisfied SAME_NUMA requirement\n"; return 1; }
  req.minimum_locality = LocalityClass::REMOTE_NUMA_DOMAIN;
  so = f.select(req);
  if (!so.ok) { std::cout << "FAIL: REMOTE_NUMA should satisfy itself\n"; return 1; }
  std::cout << "PASS: locality_policy\n";
  return 0;
}
