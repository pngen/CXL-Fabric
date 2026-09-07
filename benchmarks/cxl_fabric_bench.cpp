// ============================================================================
// CXL Fabric - CPU/runtime benchmark (legitimate operations only; no synthetic
// hardware performance is ever reported as measured).
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/accounting.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include "cxl_fabric/persistence/persistence.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
double seconds(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double>(b - a).count();
}
void report(const std::string& name, std::size_t ops, double secs) {
  double rate = secs > 0 ? double(ops) / secs : 0.0;
  std::cout << name << ": ops=" << ops << " elapsed_s=" << secs << " ops_per_sec=" << rate << "\n";
}
}

int main() {
  using namespace cxl_fabric;
  const std::size_t N = 50000;
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  HostId host("host-bench");
  for (auto& d : synthetic::baseline_devices(host)) f.register_device(d, ctx);
  f.create_pool(synthetic::make_pool(PoolId("p-1"), PoolGeneration(1), {
      {DeviceId("dev-1"), DeviceGeneration(1), 16ull*1024*1024*1024, true, true, "fd-a", LocalityClass::SAME_HOST},
      {DeviceId("dev-2"), DeviceGeneration(1), 64ull*1024*1024*1024, true, true, "fd-b", LocalityClass::REMOTE_NUMA_DOMAIN} }), ctx);
  f.create_region(synthetic::make_region(RegionId("r1"), RegionGeneration(1), PoolId("p-1"), {DeviceId("dev-1")},
      16ull*1024*1024*1024, LocalityClass::SAME_HOST, TierClass::CXL_NEAR, RegionState::ONLINE), ctx);
  f.create_region(synthetic::make_region(RegionId("r2"), RegionGeneration(1), PoolId("p-1"), {DeviceId("dev-2")},
      64ull*1024*1024*1024, LocalityClass::REMOTE_NUMA_DOMAIN, TierClass::CXL_REMOTE, RegionState::ONLINE), ctx);

  auto t0 = Clock::now();
  for (std::size_t i = 0; i < N; ++i) { StateSnapshot s = f.snapshot(); (void)s.devices.size(); }
  report("snapshot_lookup", N, seconds(t0, Clock::now()));

  AdmissionRequest req; req.bytes=1ull<<30; req.consumer=ConsumerId("svc");
  t0 = Clock::now();
  for (std::size_t i = 0; i < N; ++i) { SelectionOutcome so = f.select(req); (void)so.ok; }
  report("pool_selection", N, seconds(t0, Clock::now()));

  // reservation create/commit/release cycle (accounting stays exact)
  std::vector<ReservationId> created; created.reserve(N);
  t0 = Clock::now();
  for (std::size_t i = 0; i < N; ++i) {
    ReserveOutcome ro = f.reserve(req, ctx);
    if (ro.ok) { f.commit(ro.reservation.id, ctx); f.release(ro.reservation.id, ctx); created.push_back(ro.reservation.id); }
  }
  report("reservation_create_commit_release", N, seconds(t0, Clock::now()));

  // serialization round-trip
  PersistedState st; std::vector<std::uint8_t> payload; std::string err;
  f.export_persisted(st);
  t0 = Clock::now();
  for (std::size_t i = 0; i < N; ++i) { serialize_state(st, payload, err); }
  report("serialize_state", N, seconds(t0, Clock::now()));

  AuditReport ar = f.audit();
  std::cout << "audit_ok=" << (ar.ok ? "true" : "false") << "\\n";
  return 0;
}