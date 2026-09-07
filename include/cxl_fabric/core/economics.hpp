// ============================================================================
// CXL Fabric - access economics model (locality, latency, bandwidth, cost)
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/enums.hpp"
#include "cxl_fabric/core/identity.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cxl_fabric {

// Nominal access economics for a CXL capacity candidate. Nothing here is a
// measured benchmark unless evidence_class == MEASURED; every number carries
// its own provenance.
struct AccessEconomics {
  LocalityClass locality = LocalityClass::UNKNOWN;
  TierClass tier = TierClass::CXL_UNKNOWN;
  EvidenceClass evidence_class = EvidenceClass::UNKNOWN;
  Provenance provenance = Provenance::UNSUPPORTED;
  std::uint64_t estimated_latency_ns = 0;       // 0 == unknown
  std::uint64_t estimated_bandwidth_mbps = 0;   // 0 == unknown
  std::uint32_t hop_count = 0;                  // 0 == none/direct
  double access_penalty_vs_local = 1.0;         // 1.0 == equal to local DRAM
  std::uint64_t migration_cost_bytes = 0;       // amortized cost estimate
  bool failsafe_valid = false;                  // evidence is current
  std::string notes;

  // Named latency/cost classes used for policy comparison.
  enum class LatencyClass { UNKNOWN, NEAR, LOCAL, SHARED, REMOTE, CROSS_HOST };
  LatencyClass latency_class() const {
    switch (locality) {
      case LocalityClass::DIRECT_LOCAL: return LatencyClass::NEAR;
      case LocalityClass::SAME_HOST:
      case LocalityClass::SAME_NUMA_DOMAIN: return LatencyClass::LOCAL;
      case LocalityClass::REMOTE_NUMA_DOMAIN:
      case LocalityClass::SAME_CXL_ROOT:
      case LocalityClass::SAME_SWITCH:
      case LocalityClass::SWITCH_REMOTE:
      case LocalityClass::MULTI_HOP_CXL: return LatencyClass::REMOTE;
      case LocalityClass::CROSS_HOST: return LatencyClass::CROSS_HOST;
      case LocalityClass::UNKNOWN: return LatencyClass::UNKNOWN;
    }
    return LatencyClass::UNKNOWN;
  }

  bool more_local_than(const AccessEconomics& o) const {
    return ordinal() < o.ordinal();
  }
private:
  int ordinal() const {
    switch (locality) {
      case LocalityClass::DIRECT_LOCAL: return 0;
      case LocalityClass::SAME_HOST: return 1;
      case LocalityClass::SAME_NUMA_DOMAIN: return 2;
      case LocalityClass::REMOTE_NUMA_DOMAIN: return 3;
      case LocalityClass::SAME_CXL_ROOT: return 4;
      case LocalityClass::SAME_SWITCH: return 5;
      case LocalityClass::SWITCH_REMOTE: return 6;
      case LocalityClass::MULTI_HOP_CXL: return 7;
      case LocalityClass::CROSS_HOST: return 8;
      case LocalityClass::UNKNOWN: return 9;
    }
    return 9;
  }
};

}  // namespace cxl_fabric
