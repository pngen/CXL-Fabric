// ============================================================================
// CXL Fabric - CXL region record
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/enums.hpp"
#include "cxl_fabric/core/identity.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace cxl_fabric {

// A CXL memory region: an interleave/decoder/window-backed set of member
// devices that exposes governed capacity. A region being persisted does not
// make its physical capacity current after a restart.
struct CxlRegion {
  RegionId id;
  RegionGeneration generation;
  PoolId pool;                           // owning pool
  std::vector<DeviceId> member_devices;   // ordered membership
  std::string interleave_set;             // interleave characterization
  std::uint64_t region_capacity_bytes = 0;  // governable capacity in this region
  bool interleaved = false;
  RegionState state = RegionState::DEFINED;
  std::vector<DecoderId> decoders;
  std::vector<WindowId> windows;
  std::string access_mode;                // shared / dedicated / ...
  ConsumerId consumer_scope;              // restricted consumer if any
  LocalityClass locality = LocalityClass::UNKNOWN;
  TierClass tier = TierClass::CXL_UNKNOWN;
  HealthState health = HealthState::UNKNOWN;
  std::uint64_t reserved_bytes = 0;
  std::uint64_t committed_bytes = 0;
  Provenance provenance = Provenance::UNSUPPORTED;
  EvidenceGeneration evidence_generation;
  bool evidence_current = false;
  bool device_local = false;
};

}  // namespace cxl_fabric
