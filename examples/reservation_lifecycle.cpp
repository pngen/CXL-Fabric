
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <iostream>

static void mk(cxl_fabric::Fabric& f, cxl_fabric::AuthorityContext& ctx) {
  using namespace cxl_fabric;
  HostId host("host-r");
  CxlDevice d = synthetic::make_device(DeviceId("d1"), DeviceGeneration(1), DeviceBootId("b1"), host,
      16ull*1024*1024*1024, VolatilityKind::VOLATILE, SharingMode::DEDICATED,
      LocalityClass::SAME_HOST, 200, 16000, 1, "fd");
  f.register_device(d, ctx);
  f.create_pool(synthetic::make_pool(PoolId("p-1"), PoolGeneration(1),
      {{DeviceId("d1"), DeviceGeneration(1), 16ull*1024*1024*1024, true, true, "fd", LocalityClass::SAME_HOST}}), ctx);
  f.create_region(synthetic::make_region(RegionId("r1"), RegionGeneration(1), PoolId("p-1"),
      {DeviceId("d1")}, 16ull*1024*1024*1024, LocalityClass::SAME_HOST, TierClass::CXL_NEAR, RegionState::ONLINE), ctx);
}

int main() {
  using namespace cxl_fabric;
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  mk(f, ctx);
  ((void)0);
  AdmissionRequest req; req.bytes=4ull*1024*1024*1024; req.consumer=ConsumerId("svc");
  ReserveOutcome ro = f.reserve(req, ctx);
  if (!ro.ok) { std::cout << "FAIL: reserve\n"; return 1; }
  if (ro.reservation.state != ResumeState::RESERVED) { std::cout << "FAIL: not reserved\n"; return 1; }
  ReserveOutcome co = f.commit(ro.reservation.id, ctx);
  if (!co.ok) { std::cout << "FAIL: commit\n"; return 1; }
  if (co.reservation.state != ResumeState::COMMITTED) { std::cout << "FAIL: not committed\n"; return 1; }
  ReserveOutcome rr = f.release(ro.reservation.id, ctx);
  if (!rr.ok) { std::cout << "FAIL: release\n"; return 1; }
  if (rr.reservation.state != ResumeState::RELEASED) { std::cout << "FAIL: not released\n"; return 1; }
  AuditReport ar = f.audit();
  std::cout << "audit_ok=" << (ar.ok ? "true" : "false") << " free=" << ar.global_ledger.free() << "\n";
  if (!ar.ok) { for (auto& e : ar.errors) std::cout << "  " << e << "\n"; return 1; }
  std::cout << "PASS: reservation_lifecycle\n";
  return 0;
}
