// ============================================================================
// CXL Fabric - deterministic synthetic CXL topology (SYNTHETIC provenance)
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/device.hpp"
#include "cxl_fabric/core/enum_strings.hpp"
#include "cxl_fabric/core/pool.hpp"
#include "cxl_fabric/core/region.hpp"

namespace cxl_fabric {
namespace synthetic {

inline CxlDevice make_device(const DeviceId& id, const DeviceGeneration& gen,
                             const DeviceBootId& boot, const HostId& host,
                             std::uint64_t bytes, VolatilityKind vol, SharingMode sharing,
                             LocalityClass loc, std::uint64_t latency_ns, std::uint64_t bw_mbps,
                             std::uint32_t hops, const std::string& failure_domain) {
  CxlDevice d;
  d.id = id; d.generation = gen; d.boot_id = boot;
  d.raw_identity = "synthetic:" + id.str();
  d.vendor_device_id = "VEND:0001 DEVI:0001";
  d.cxl_device_class = "Type-3";
  d.device_type = "memory";
  d.host = host;
  d.total_physical_bytes = bytes;
  d.online_bytes = bytes;
  d.volatility = vol;
  d.sharing = sharing;
  d.multi_host_capable = false;
  d.interleave_capable = (vol == VolatilityKind::VOLATILE);
  d.accelerator_access = AcceleratorAccessState::HOST_MEDIATED;
  d.health = HealthState::HEALTHY;
  d.state = DeviceState::ONLINE;
  d.provenance = Provenance::SYNTHETIC;
  d.evidence_current = true;
  d.evidence_generation = EvidenceGeneration{1};
  d.lifecycle_note = "synthetic";
  d.economics.locality = loc;
  d.economics.tier = (vol == VolatilityKind::PERSISTENT) ? TierClass::CXL_PERSISTENT : TierClass::CXL_CAPACITY;
  d.economics.evidence_class = EvidenceClass::SYNTHETIC;
  d.economics.provenance = Provenance::SYNTHETIC;
  d.economics.estimated_latency_ns = latency_ns;
  d.economics.estimated_bandwidth_mbps = bw_mbps;
  d.economics.hop_count = hops;
  d.economics.access_penalty_vs_local = 1.0 + double(hops) * 0.25;
  d.economics.migration_cost_bytes = bytes / 8u;
  d.economics.failsafe_valid = true;
  d.economics.notes = failure_domain;
  // Capability evidence (SYNTHETIC).
  auto addcap = [&](CapabilityClass k, SupportState s) {
    Capability c; c.klass = k; c.state = s; c.version = 1;
    c.provenance = Provenance::SYNTHETIC; c.evidence_current = true;
    c.evidence_generation = EvidenceGeneration{1}; d.capabilities.set(c);
  };
  addcap(CapabilityClass::MEMORY_DEVICE, SupportState::SUPPORTED);
  addcap(CapabilityClass::TYPE3_MEMORY, SupportState::SUPPORTED);
  addcap(CapabilityClass::VOLATILE_CAPACITY, SupportState::SUPPORTED);
  addcap(CapabilityClass::DYNAMIC_CAPACITY, SupportState::SUPPORTED);
  d.capabilities.set([&](Capability&& c){ c.klass=CapabilityClass::PERSISTENT_CAPACITY;
    c.state=(vol==VolatilityKind::PERSISTENT)?SupportState::SUPPORTED:SupportState::UNSUPPORTED;
    c.provenance=Provenance::SYNTHETIC; c.evidence_current=true; return c; }(Capability{}));
  return d;
}

inline CxlRegion make_region(const RegionId& id, const RegionGeneration& gen, const PoolId& pool,
                             const std::vector<DeviceId>& members, std::uint64_t bytes,
                             LocalityClass loc, TierClass tier, RegionState st) {
  CxlRegion r;
  r.id = id; r.generation = gen; r.pool = pool; r.member_devices = members;
  r.region_capacity_bytes = bytes; r.interleaved = members.size() > 1;
  r.state = st; r.access_mode = "dedicated"; r.locality = loc; r.tier = tier;
  r.health = HealthState::HEALTHY; r.provenance = Provenance::SYNTHETIC;
  r.evidence_current = true; r.evidence_generation = EvidenceGeneration{1};
  return r;
}

inline CxlPool make_pool(const PoolId& id, const PoolGeneration& gen,
                         const std::vector<PoolMember>& members) {
  CxlPool p; p.id = id; p.generation = gen; p.members = members;
  p.admission_policy = "default"; p.state = PoolState::AVAILABLE;
  p.health_summary = HealthState::HEALTHY; p.provenance = Provenance::SYNTHETIC;
  p.evidence_current = true; p.evidence_generation = EvidenceGeneration{1};
  return p;
}

// Deterministic baseline topology used by CLI, examples, and tests. It never
// fabricates real hardware; every record carries SYNTHETIC provenance.
inline std::vector<CxlDevice> baseline_devices(const HostId& host) {
  return {
    make_device(DeviceId("dev-1"), DeviceGeneration{1}, DeviceBootId("boot-1"), host,
                16ull * 1024 * 1024 * 1024, VolatilityKind::VOLATILE, SharingMode::DEDICATED,
                LocalityClass::SAME_HOST, 180, 32000, 1, "fd-root-a"),
    make_device(DeviceId("dev-2"), DeviceGeneration{1}, DeviceBootId("boot-2"), host,
                64ull * 1024 * 1024 * 1024, VolatilityKind::VOLATILE, SharingMode::SHARED,
                LocalityClass::REMOTE_NUMA_DOMAIN, 400, 16000, 3, "fd-root-b"),
    make_device(DeviceId("dev-3"), DeviceGeneration{1}, DeviceBootId("boot-3"), host,
                8ull * 1024 * 1024 * 1024, VolatilityKind::PERSISTENT, SharingMode::DEDICATED,
                LocalityClass::SAME_CXL_ROOT, 260, 24000, 2, "fd-root-a"),
  };
}

}  // namespace synthetic
}  // namespace cxl_fabric