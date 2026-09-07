
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <iostream>
int main(){
  using namespace cxl_fabric;
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  HostId host("host-sg");
  CxlDevice d = synthetic::make_device(DeviceId("d1"), DeviceGeneration(1), DeviceBootId("b1"), host,
      16ull*1024*1024*1024, VolatilityKind::VOLATILE, SharingMode::DEDICATED, LocalityClass::SAME_HOST, 200,16000,1,"fd");
  f.register_device(d, ctx);
  f.create_pool(synthetic::make_pool(PoolId("p-1"), PoolGeneration(1),
      {{DeviceId("d1"), DeviceGeneration(1), 16ull*1024*1024*1024, true,true,"fd",LocalityClass::SAME_HOST}}), ctx);
  f.create_region(synthetic::make_region(RegionId("r1"), RegionGeneration(1), PoolId("p-1"), {DeviceId("d1")},
      16ull*1024*1024*1024, LocalityClass::SAME_HOST, TierClass::CXL_NEAR, RegionState::ONLINE), ctx);
  AdmissionRequest req; req.bytes=4ull*1024*1024*1024; req.consumer=ConsumerId("svc");
  ReserveOutcome ro = f.reserve(req, ctx);
  if (!ro.ok) { std::cout << "FAIL: reserve\n"; return 1; }
  // Advance pool generation via a member change -> old reservation can no longer commit.
  PoolMember m; m.device=DeviceId("d2"); m.device_generation=DeviceGeneration(1); m.nominal_bytes=8ull*1024*1024*1024; m.online=true; m.health_ok=true; m.failure_domain="fd2";
  f.register_device(synthetic::make_device(DeviceId("d2"), DeviceGeneration(1), DeviceBootId("b2"), host,
      8ull*1024*1024*1024, VolatilityKind::VOLATILE, SharingMode::DEDICATED, LocalityClass::SAME_HOST, 210,15000,1,"fd2"), ctx);
  f.add_pool_member(PoolId("p-1"), m, ctx);
  ReserveOutcome co = f.commit(ro.reservation.id, ctx);
  if (co.ok) { std::cout << "FAIL: stale pool-generation commit succeeded\n"; return 1; }
  if (co.reason != DecisionReason::POOL_GENERATION_STALE) { std::cout << "FAIL: wrong reason " << to_string(co.reason) << "\n"; return 1; }
  std::cout << "PASS: stale_generation\n";
  return 0;
}