
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <iostream>
int main(){
  using namespace cxl_fabric;
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  HostId host("host-scp");
  for (auto& d : synthetic::baseline_devices(host)) f.register_device(d, ctx);
  auto mk = [&](const char* id, std::uint64_t bytes, const char* fd){
    PoolMember m; m.device=DeviceId(id); m.device_generation=DeviceGeneration(1);
    m.nominal_bytes=bytes; m.online=true; m.health_ok=true; m.failure_domain=fd;
    m.locality=LocalityClass::SAME_HOST; return m; };
  CxlPool pool = synthetic::make_pool(PoolId("p-1"), PoolGeneration(1), {
      mk("dev-1", 16ull*1024*1024*1024, "fd-a"),
      mk("dev-2", 64ull*1024*1024*1024, "fd-b"),
      mk("dev-3", 8ull*1024*1024*1024, "fd-a") });
  f.create_pool(pool, ctx);
  // Verify pool capacity sums exactly = 16+64+8 GiB, each member counted once.
  StateSnapshot s = f.snapshot();
  CxlPool got = s.pools.at(PoolId("p-1"));
  std::uint64_t expect = (16+64+8ull)*1024*1024*1024;
  std::cout << "pool_total=" << got.total_governed_bytes << " expected=" << expect << "\n";
  if (got.total_governed_bytes != expect) { std::cout << "FAIL: pool capacity mismatch\n"; return 1; }
  AuditReport ar = f.audit();
  if (!ar.ok) { for (auto& e : ar.errors) std::cout << "  " << e << "\n"; return 1; }
  std::cout << "PASS: synthetic_cxl_pool\n";
  return 0;
}
