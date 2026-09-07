
// ============================================================================
// CXL Fabric - cxl-fabric CLI
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#include "cxl_fabric/backend/backend.hpp"
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/accounting.hpp"
#include "cxl_fabric/core/enum_strings.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace cxl_fabric;

// Populate a Fabric with the deterministic synthetic baseline topology. Records
// carry SYNTHETIC provenance; nothing here is physical CXL hardware.
void setup_synthetic(Fabric& f) {
  AuthorityContext ctx;
  ctx.coordinator_epoch = f.epoch();
  ctx.policy_generation = f.policy_generation();
  ctx.consumer_generation = ConsumerGeneration(1);
  HostId host("host-synth");
  for (const auto& d : synthetic::baseline_devices(host)) f.register_device(d, ctx);

  auto mk_member = [&](const char* id, std::uint64_t bytes, const char* fd) {
    PoolMember m;
    m.device = DeviceId(id);
    m.device_generation = DeviceGeneration(1);
    m.nominal_bytes = bytes;
    m.online = true;
    m.health_ok = true;
    m.failure_domain = fd;
    m.locality = LocalityClass::SAME_HOST;
    return m;
  };
  CxlPool pool = synthetic::make_pool(PoolId("p-1"), PoolGeneration(1), {
      mk_member("dev-1", 16ull*1024*1024*1024, "fd-root-a"),
      mk_member("dev-2", 64ull*1024*1024*1024, "fd-root-b"),
      mk_member("dev-3", 8ull*1024*1024*1024, "fd-root-a") });
  f.create_pool(pool, ctx);

  CxlRegion r1 = synthetic::make_region(RegionId("reg-1"), RegionGeneration(1), PoolId("p-1"),
      {DeviceId("dev-1")}, 16ull*1024*1024*1024, LocalityClass::SAME_HOST, TierClass::CXL_NEAR, RegionState::ONLINE);
  CxlRegion r2 = synthetic::make_region(RegionId("reg-2"), RegionGeneration(1), PoolId("p-1"),
      {DeviceId("dev-2")}, 64ull*1024*1024*1024, LocalityClass::REMOTE_NUMA_DOMAIN, TierClass::CXL_REMOTE, RegionState::ONLINE);
  CxlRegion r3 = synthetic::make_region(RegionId("reg-3"), RegionGeneration(1), PoolId("p-1"),
      {DeviceId("dev-3")}, 8ull*1024*1024*1024, LocalityClass::SAME_CXL_ROOT, TierClass::CXL_PERSISTENT, RegionState::ONLINE);
  f.create_region(r1, ctx);
  f.create_region(r2, ctx);
  f.create_region(r3, ctx);
}

std::string gbytes(std::uint64_t b) {
  return std::to_string(b / (1024ull*1024*1024)) + "GiB";
}

int cmd_discover(int argc, char** argv) {
  std::string backend = "synthetic";
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--backend" && i + 1 < argc) backend = argv[++i];
  }
  std::unique_ptr<IDiscoveryBackend> b;
  if (backend == "system") b = create_system_backend();
  else if (backend == "unsupported") b = create_unsupported_backend();
  else b = create_synthetic_backend();
  DiscoveryResult r = b->discover();
  std::cout << "backend=" << r.backend_name << " host=" << r.host_id.str() << "\n";
  std::cout << "cxl_memory_supported=" << (r.cxl_memory_supported ? "true" : "false")
            << " provenance=" << to_string(r.provenance) << " devices=" << r.devices.size() << "\n";
  for (const auto& d : r.devices) {
    std::cout << "  device " << d.id.str() << " gen=" << d.generation.value()
              << " vol=" << to_string(d.volatility) << " state=" << to_string(d.state)
              << " provenance=" << to_string(d.provenance) << " total=" << gbytes(d.total_physical_bytes) << "\n";
  }
  for (const auto& n : r.notes) std::cout << "  note: " << n << "\n";
  for (const auto& a : r.accelerators) std::cout << "  accelerator: " << a << "\n";
  return 0;
}

}  // namespace

using namespace cxl_fabric;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cout << "usage: cxl-fabric <discover|devices|regions|pools|capacity|explain|validate-state|synthetic-demo> [args]\n";
    return 1;
  }
  std::string cmd = argv[1];
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  setup_synthetic(f);
  AuthorityContext ctx; ctx.coordinator_epoch = f.epoch(); ctx.policy_generation = f.policy_generation();
  ctx.consumer_generation = ConsumerGeneration(1);

  if (cmd == "discover") return cmd_discover(argc, argv);
  if (cmd == "devices") {
    StateSnapshot s = f.snapshot();
    for (const auto& kv : s.devices) {
      const CxlDevice& d = kv.second;
      std::cout << "device " << d.id.str() << " gen=" << d.generation.value()
                << " state=" << to_string(d.state) << " prov=" << to_string(d.provenance)
                << " vol=" << to_string(d.volatility) << " host=" << d.host.str()
                << " evidence_current=" << (d.evidence_current ? "true" : "false") << "\n";
    }
    return 0;
  }
  if (cmd == "regions") {
    StateSnapshot s = f.snapshot();
    for (const auto& kv : s.regions) {
      const CxlRegion& r = kv.second;
      std::cout << "region " << r.id.str() << " gen=" << r.generation.value() << " pool=" << r.pool.str()
                << " state=" << to_string(r.state) << " tier=" << to_string(r.tier)
                << " cap=" << gbytes(r.region_capacity_bytes) << "\n";
    }
    return 0;
  }
  if (cmd == "pools") {
    StateSnapshot s = f.snapshot();
    for (const auto& kv : s.pools) {
      const CxlPool& p = kv.second;
      std::cout << "pool " << p.id.str() << " gen=" << p.generation.value()
                << " state=" << to_string(p.state)
                << " total=" << gbytes(p.total_governed_bytes)
                << " committed=" << gbytes(p.committed_bytes)
                << " reserved=" << gbytes(p.reserved_bytes)
                << " free=" << gbytes(p.free_bytes()) << "\n";
    }
    return 0;
  }
  if (cmd == "capacity") {
    AuditReport ar = f.audit();
    const CapacityLine& g = ar.global_ledger;
    std::cout << "governed_total=" << gbytes(g.total) << " online=" << gbytes(g.online)
              << " unavailable=" << gbytes(g.unavailable) << " reserved=" << gbytes(g.reserved)
              << " committed=" << gbytes(g.committed) << " free=" << gbytes(g.free()) << "\n";
    std::cout << "audit_ok=" << (ar.ok ? "true" : "false") << "\n";
    for (const auto& e : ar.errors) std::cout << "  error: " << e << "\n";
    return 0;
  }
  if (cmd == "validate-state") {
    AuditReport ar = f.audit();
    std::cout << "validate-state " << (ar.ok ? "OK" : "FAIL") << "\n";
    for (const auto& e : ar.errors) std::cout << "  " << e << "\n";
    return ar.ok ? 0 : 1;
  }
  if (cmd == "explain") {
    AdmissionRequest req;
    req.bytes = 4ull*1024*1024*1024;
    req.consumer = ConsumerId("gpu0");
    for (int i = 2; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--bytes" && i + 1 < argc) req.bytes = std::stoull(argv[++i]);
      else if (a == "--consumer" && i + 1 < argc) req.consumer = ConsumerId(argv[++i]);
      else if (a == "--persistent") req.require_persistence = true;
      else if (a == "--locality" && i + 1 < argc) {
        auto l = from_string(argv[++i], static_cast<LocalityClass*>(nullptr));
        if (l) req.minimum_locality = *l;
      }
    }
    SelectionOutcome so = f.select(req);
    for (const auto& c : so.candidates) {
      std::cout << "candidate pool=" << c.pool << " region=" << c.region
                << " selected=" << (c.selected ? "yes" : "no");
      if (!c.selected) std::cout << " reason=" << to_string(c.reason);
      std::cout << "\n";
      for (const auto& fac : c.factors) std::cout << "    " << fac.name << "=" << (fac.pass ? "pass" : "fail") << " " << fac.detail << "\n";
    }
    std::cout << "outcome ok=" << (so.ok ? "true" : "false");
    if (so.ok) std::cout << " chosen_pool=" << so.chosen.pool.str() << " chosen_region=" << so.chosen.region.str();
    std::cout << "\n";
    return 0;
  }
  if (cmd == "synthetic-demo") {
    std::cout << "synthetic-demo: baseline synthetic CXL topology (SYNTHETIC provenance)\n";
    SelectionOutcome so = f.select(AdmissionRequest{});
    std::cout << "admission 0 bytes candidates=" << so.candidates.size() << "\n";
    ReserveOutcome ro = f.reserve([]{ AdmissionRequest r; r.bytes=1ull<<30; r.consumer=ConsumerId("svc"); return r; }(), ctx);
    std::cout << "reserve 1GiB ok=" << (ro.ok ? "true" : "false");
    if (ro.ok) std::cout << " id=" << ro.reservation.id.str();
    std::cout << "\n";
    AuditReport ar = f.audit();
    std::cout << "audit_ok=" << (ar.ok ? "true" : "false") << "\n";
    return 0;
  }
  std::cout << "unknown command: " << cmd << "\n";
  return 1;
}
