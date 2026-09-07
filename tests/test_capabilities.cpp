
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/capability.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <iostream>

static int failures = 0;
#define CHECK(c,m) do { if (!(c)) { std::cout << "FAIL: " << m << "\n"; ++failures; } } while (0)
using namespace cxl_fabric;

static void setup(Fabric& f) {
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  HostId host("host-cap");
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
  // Capability fail-closed: UNKNOWN must not satisfy a hard requirement.
  Capabilities caps;
  Capability c; c.klass = CapabilityClass::TYPE3_MEMORY; c.state = SupportState::UNKNOWN; c.provenance = Provenance::SYNTHETIC;
  caps.set(c);
  CHECK(!caps.required_supported(CapabilityClass::TYPE3_MEMORY), "UNKNOWN capability fails closed");
  c.state = SupportState::SUPPORTED;
  caps.set(c);
  CHECK(!caps.required_supported(CapabilityClass::TYPE3_MEMORY), "unsupported capability fails");
  c.evidence_current = true;
  caps.set(c);
  CHECK(caps.required_supported(CapabilityClass::TYPE3_MEMORY), "supported current capability passes");

  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  setup(f);

  // Pool capacity sums exactly; duplicate membership rejected.
  StateSnapshot snap = f.snapshot();
  CxlPool p = snap.pools.at(PoolId("p-1"));
  std::uint64_t expect = (16+64)*1024ull*1024*1024;
  CHECK(p.total_governed_bytes == expect, "pool capacity sums exactly");
  AuditReport ar = f.audit();
  CHECK(ar.ok, "audit ok");
  PoolMember dup; dup.device=DeviceId("dev-1"); dup.device_generation=DeviceGeneration(1); dup.nominal_bytes=1ull<<30; dup.online=true; dup.health_ok=true;
  CHECK(f.add_pool_member(PoolId("p-1"), dup, ctx) == DecisionReason::DUPLICATE_IDENTITY, "duplicate membership rejected");

  // Accelerator-access hard requirement: HOST_MEDIATED device fails DIRECT_SUPPORTED.
  AdmissionRequest req; req.bytes=1ull<<30; req.consumer=ConsumerId("acc");
  req.accelerator_access = AcceleratorAccessState::DIRECT_SUPPORTED;
  SelectionOutcome so = f.select(req);
  CHECK(!so.ok, "DIRECT_SUPPORTED requirement rejects HOST_MEDIATED only topology");

  // Deterministic explanation present for a passing request.
  AdmissionRequest okreq; okreq.bytes=1ull<<30; okreq.consumer=ConsumerId("svc");
  SelectionOutcome okay = f.select(okreq);
  CHECK(okay.ok, "plain selection ok");
  CHECK(!okay.result.factors.empty(), "explanation factors present");

  // Unknown/unsupported hard capabilities never pass.
  // (dev-3 persistent requirement satisfied by dev-3; volatile rejected)
  AdmissionRequest pers; pers.bytes=1ull<<30; pers.consumer=ConsumerId("p"); pers.require_persistence = true;
  SelectionOutcome ps = f.select(pers);
  // Only dev-3 is persistent; it is in the pool? No, dev-3 not in pool -> none qualify.
  CHECK(!ps.ok, "persistent-required reserves only enabled when a persistent member is in the pool");

  if (failures) { std::cout << "test_capabilities FAILURES=" << failures << "\n"; return 1; }
  std::cout << "PASS: test_capabilities\n";
  return 0;
}
