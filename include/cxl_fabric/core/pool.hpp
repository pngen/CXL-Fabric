// ============================================================================
// CXL Fabric - CXL pool record
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/enums.hpp"
#include "cxl_fabric/core/identity.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace cxl_fabric {

// A pool member is a generation-bound reference to a device plus the facts that
// make that reference usable for governed capacity. Membership is a *claim*,
// not proof of current eligibility: a stale device generation cannot be used.
struct PoolMember {
  DeviceId device;
  DeviceGeneration device_generation;   // generation this membership was validated against
  std::uint64_t nominal_bytes = 0;      // bytes this member contributes, if known
  bool online = false;
  bool health_ok = false;
  std::string failure_domain;           // failure-domain key (device/port/switch/root/host)
  LocalityClass locality = LocalityClass::UNKNOWN;
};

struct CxlPool {
  PoolId id;
  PoolGeneration generation;
  std::vector<PoolMember> members;
  std::uint64_t total_governed_bytes = 0;
  std::uint64_t committed_bytes = 0;
  std::uint64_t reserved_bytes = 0;
  std::uint64_t draining_bytes = 0;
  std::uint64_t unavailable_bytes = 0;
  std::string admission_policy;
  std::vector<std::string> failure_domains;   // distinct keys
  LocalityClass locality_distribution = LocalityClass::UNKNOWN;
  HealthState health_summary = HealthState::UNKNOWN;
  PoolState state = PoolState::DEFINED;
  Provenance provenance = Provenance::UNSUPPORTED;
  EvidenceGeneration evidence_generation;
  bool evidence_current = false;
  std::string lifecycle_note;

  // free is exactly what remains governable for new reservations.
  std::uint64_t free_bytes() const {
    std::uint64_t online = total_governed_bytes;
    if (online <= unavailable_bytes) return 0;
    std::uint64_t used = unavailable_bytes + reserved_bytes + committed_bytes + draining_bytes;
    if (online <= used) return 0;
    return online - used;
  }
};

}  // namespace cxl_fabric
