// ============================================================================
// CXL Fabric - core enumerations
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace cxl_fabric {

// Provenance of an item of evidence or capability ---------------------------
enum class Provenance {
  REAL,       // directly observed from OS / documented runtime API / hardware action
  DERIVED,    // computed from REAL facts
  SYNTHETIC,  // constructed test topology/device
  UNSUPPORTED // capability cannot be proven or is absent
};

enum class HealthState {
  UNKNOWN,
  HEALTHY,
  DEGRADED,
  POISONED,
  FAILED,
  REVALIDATION_REQUIRED
};

enum class DeviceState {
  DISCOVERED,
  ENUMERATED,
  AVAILABLE,
  ONLINE,
  DEGRADED,
  DRAINING,
  REVALIDATION_REQUIRED,
  OFFLINE,
  FAILED,
  REMOVED,
  RETIRED
};

enum class RegionState {
  DEFINED,
  VALIDATING,
  ONLINE,
  DEGRADED,
  DRAINING,
  RECONFIGURING,
  REVALIDATION_REQUIRED,
  OFFLINE,
  FAILED,
  RETIRED
};

enum class PoolState {
  DEFINED,
  AVAILABLE,
  DEGRADED,
  DRAINING,
  REVALIDATION_REQUIRED,
  FAILED,
  RETIRED
};

enum class CapabilityClass {
  MEMORY_DEVICE,
  TYPE3_MEMORY,
  HOST_MANAGED_DEVICE_MEMORY,
  VOLATILE_CAPACITY,
  PERSISTENT_CAPACITY,
  DYNAMIC_CAPACITY,
  HOTPLUG,
  MULTI_HOST_ACCESS,
  MEMORY_POOLING,
  INTERLEAVE,
  DEVICE_DECODER,
  HOST_DECODER,
  SWITCH_ATTACHED,
  DIRECT_ATTACHED,
  ERROR_REPORTING,
  POISON_REPORTING,
  CAPACITY_RESIZE,
  REGION_RECONFIGURATION,
  ACCELERATOR_VISIBLE_PATH,
  CACHE_COHERENT_ACCESS
};

enum class SupportState {
  SUPPORTED,
  UNSUPPORTED,
  UNKNOWN,
  REVALIDATION_REQUIRED
};

enum class EvidenceClass {  // economic / measurement evidence class
  MEASURED,
  PLATFORM_REPORTED,
  CONFIGURED,
  DERIVED,
  SYNTHETIC,
  UNKNOWN
};

enum class LocalityClass {
  DIRECT_LOCAL,
  SAME_HOST,
  SAME_NUMA_DOMAIN,
  REMOTE_NUMA_DOMAIN,
  SAME_CXL_ROOT,
  SAME_SWITCH,
  SWITCH_REMOTE,
  MULTI_HOP_CXL,
  CROSS_HOST,
  UNKNOWN
};

enum class TierClass {
  CXL_NEAR,
  CXL_LOCAL,
  CXL_SHARED,
  CXL_REMOTE,
  CXL_PERSISTENT,
  CXL_CAPACITY,
  CXL_UNKNOWN
};

enum class AcceleratorAccessState {
  DIRECT_SUPPORTED,
  INDIRECT_SUPPORTED,
  HOST_MEDIATED,
  UNSUPPORTED,
  UNKNOWN,
  REVALIDATION_REQUIRED
};

enum class ResumeState {
  RESERVED,
  COMMITTED,
  RELEASED,
  FENCED
};

enum class VolatilityKind {
  VOLATILE,
  PERSISTENT,
  UNKNOWN
};

enum class SharingMode {
  DEDICATED,
  SHARED,
  UNKNOWN
};

enum class FailoverOutcome {
  KEEP,
  DRAIN,
  FAILOVER,
  REVALIDATE,
  REJECT,
  DEGRADED,
  LOST
};

enum class FailureDomainLevel {
  NONE,
  DEVICE,
  PORT,
  SWITCH,
  ROOT_COMPLEX,
  HOST,
  RACK
};

// Selection reason codes ----------------------------------------------------
enum class DecisionReason {
  OK,
  DEVICE_STALE,
  REGION_STALE,
  POOL_STALE,
  DEVICE_NOT_ONLINE,
  REGION_NOT_ONLINE,
  EVIDENCE_STALE,
  CAPABILITY_UNSUPPORTED,
  CAPABILITY_UNKNOWN,
  CAPACITY_INSUFFICIENT,
  VOLATILITY_MISMATCH,
  HOST_INCOMPATIBLE,
  SHARING_MISMATCH,
  REQUIRED_LOCALITY_UNSATISFIED,
  ACCELERATOR_ACCESS_UNSATISFIED,
  HEALTH_UNACCEPTABLE,
  DRAINING,
  FAILED,
  POLICY_DENIED,
  REVALIDATION_REQUIRED,
  POOL_GENERATION_STALE,
  RESERVATION_STALE,
  DUPLICATE_IDENTITY,
  CONFLICTING_FACTS,
  INVALID_ARGUMENT,
  RESERVATION_CONFLICT,
  CAPACITY_OVERCOMMIT,
  PERSISTENCE_CORRUPT,
  PROTOCOL_MALFORMED,
  PROTOCOL_OVERSIZED,
  PROTOCOL_INVALID_ENUM,
  PROTOCOL_INVALID_GENERATION,
  PROTOCOL_TRAILING_GARBAGE,
  UNKNOWN_ERROR
};

// Forward declarations for parsing helpers.
template <class E>
std::optional<E> parse_enum(const std::string& s,
                            std::initializer_list<std::pair<std::string, E>> values) {
  for (const auto& p : values) {
    if (p.first == s) return p.second;
  }
  return std::nullopt;
}

inline void require(bool cond, const char* msg) {
  if (!cond) throw std::logic_error(msg);
}

// String conversions --------------------------------------------------------
namespace detail {
template <class E>
const char* label(E) = delete;
}  // namespace detail

}  // namespace cxl_fabric
