// ============================================================================
// CXL Fabric - strict enum <-> string conversions (round-trip safe)
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/enums.hpp"
#include <optional>
#include <string>
#include <utility>

namespace cxl_fabric {

// Build a from-string lookup table. Unknown strings yield std::nullopt so that
// corrupted/foreign data is rejected rather than silently coerced to a value.
template <class E>
inline std::optional<E> enum_lookup(const std::string& s,
                                    std::initializer_list<std::pair<std::string, E>> table) {
  for (const auto& p : table) {
    if (s == p.first) return p.second;
  }
  return std::nullopt;
}

// Provenance ----------------------------------------------------------------
inline const char* to_string(Provenance v) {
  switch (v) {
    case Provenance::REAL: return "REAL";
    case Provenance::DERIVED: return "DERIVED";
    case Provenance::SYNTHETIC: return "SYNTHETIC";
    case Provenance::UNSUPPORTED: return "UNSUPPORTED";
  }
  return "UNKNOWN";
}
inline std::optional<Provenance> from_string(const std::string& s, Provenance*) {
  return enum_lookup<Provenance>(s, {
      {"REAL", Provenance::REAL}, {"DERIVED", Provenance::DERIVED},
      {"SYNTHETIC", Provenance::SYNTHETIC}, {"UNSUPPORTED", Provenance::UNSUPPORTED}});
}

inline const char* to_string(HealthState v) {
  switch (v) {
    case HealthState::UNKNOWN: return "UNKNOWN";
    case HealthState::HEALTHY: return "HEALTHY";
    case HealthState::DEGRADED: return "DEGRADED";
    case HealthState::POISONED: return "POISONED";
    case HealthState::FAILED: return "FAILED";
    case HealthState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
  }
  return "UNKNOWN";
}
inline std::optional<HealthState> from_string(const std::string& s, HealthState*) {
  return enum_lookup<HealthState>(s, {
      {"UNKNOWN", HealthState::UNKNOWN}, {"HEALTHY", HealthState::HEALTHY},
      {"DEGRADED", HealthState::DEGRADED}, {"POISONED", HealthState::POISONED},
      {"FAILED", HealthState::FAILED}, {"REVALIDATION_REQUIRED", HealthState::REVALIDATION_REQUIRED}});
}

inline const char* to_string(DeviceState v) {
  switch (v) {
    case DeviceState::DISCOVERED: return "DISCOVERED";
    case DeviceState::ENUMERATED: return "ENUMERATED";
    case DeviceState::AVAILABLE: return "AVAILABLE";
    case DeviceState::ONLINE: return "ONLINE";
    case DeviceState::DEGRADED: return "DEGRADED";
    case DeviceState::DRAINING: return "DRAINING";
    case DeviceState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    case DeviceState::OFFLINE: return "OFFLINE";
    case DeviceState::FAILED: return "FAILED";
    case DeviceState::REMOVED: return "REMOVED";
    case DeviceState::RETIRED: return "RETIRED";
  }
  return "UNKNOWN";
}
inline std::optional<DeviceState> from_string(const std::string& s, DeviceState*) {
  return enum_lookup<DeviceState>(s, {
      {"DISCOVERED", DeviceState::DISCOVERED}, {"ENUMERATED", DeviceState::ENUMERATED},
      {"AVAILABLE", DeviceState::AVAILABLE}, {"ONLINE", DeviceState::ONLINE},
      {"DEGRADED", DeviceState::DEGRADED}, {"DRAINING", DeviceState::DRAINING},
      {"REVALIDATION_REQUIRED", DeviceState::REVALIDATION_REQUIRED},
      {"OFFLINE", DeviceState::OFFLINE}, {"FAILED", DeviceState::FAILED},
      {"REMOVED", DeviceState::REMOVED}, {"RETIRED", DeviceState::RETIRED}});
}

inline const char* to_string(RegionState v) {
  switch (v) {
    case RegionState::DEFINED: return "DEFINED";
    case RegionState::VALIDATING: return "VALIDATING";
    case RegionState::ONLINE: return "ONLINE";
    case RegionState::DEGRADED: return "DEGRADED";
    case RegionState::DRAINING: return "DRAINING";
    case RegionState::RECONFIGURING: return "RECONFIGURING";
    case RegionState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    case RegionState::OFFLINE: return "OFFLINE";
    case RegionState::FAILED: return "FAILED";
    case RegionState::RETIRED: return "RETIRED";
  }
  return "UNKNOWN";
}
inline std::optional<RegionState> from_string(const std::string& s, RegionState*) {
  return enum_lookup<RegionState>(s, {
      {"DEFINED", RegionState::DEFINED}, {"VALIDATING", RegionState::VALIDATING},
      {"ONLINE", RegionState::ONLINE}, {"DEGRADED", RegionState::DEGRADED},
      {"DRAINING", RegionState::DRAINING}, {"RECONFIGURING", RegionState::RECONFIGURING},
      {"REVALIDATION_REQUIRED", RegionState::REVALIDATION_REQUIRED},
      {"OFFLINE", RegionState::OFFLINE}, {"FAILED", RegionState::FAILED},
      {"RETIRED", RegionState::RETIRED}});
}

inline const char* to_string(PoolState v) {
  switch (v) {
    case PoolState::DEFINED: return "DEFINED";
    case PoolState::AVAILABLE: return "AVAILABLE";
    case PoolState::DEGRADED: return "DEGRADED";
    case PoolState::DRAINING: return "DRAINING";
    case PoolState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    case PoolState::FAILED: return "FAILED";
    case PoolState::RETIRED: return "RETIRED";
  }
  return "UNKNOWN";
}
inline std::optional<PoolState> from_string(const std::string& s, PoolState*) {
  return enum_lookup<PoolState>(s, {
      {"DEFINED", PoolState::DEFINED}, {"AVAILABLE", PoolState::AVAILABLE},
      {"DEGRADED", PoolState::DEGRADED}, {"DRAINING", PoolState::DRAINING},
      {"REVALIDATION_REQUIRED", PoolState::REVALIDATION_REQUIRED},
      {"FAILED", PoolState::FAILED}, {"RETIRED", PoolState::RETIRED}});
}

inline const char* to_string(CapabilityClass v) {
  switch (v) {
    case CapabilityClass::MEMORY_DEVICE: return "MEMORY_DEVICE";
    case CapabilityClass::TYPE3_MEMORY: return "TYPE3_MEMORY";
    case CapabilityClass::HOST_MANAGED_DEVICE_MEMORY: return "HOST_MANAGED_DEVICE_MEMORY";
    case CapabilityClass::VOLATILE_CAPACITY: return "VOLATILE_CAPACITY";
    case CapabilityClass::PERSISTENT_CAPACITY: return "PERSISTENT_CAPACITY";
    case CapabilityClass::DYNAMIC_CAPACITY: return "DYNAMIC_CAPACITY";
    case CapabilityClass::HOTPLUG: return "HOTPLUG";
    case CapabilityClass::MULTI_HOST_ACCESS: return "MULTI_HOST_ACCESS";
    case CapabilityClass::MEMORY_POOLING: return "MEMORY_POOLING";
    case CapabilityClass::INTERLEAVE: return "INTERLEAVE";
    case CapabilityClass::DEVICE_DECODER: return "DEVICE_DECODER";
    case CapabilityClass::HOST_DECODER: return "HOST_DECODER";
    case CapabilityClass::SWITCH_ATTACHED: return "SWITCH_ATTACHED";
    case CapabilityClass::DIRECT_ATTACHED: return "DIRECT_ATTACHED";
    case CapabilityClass::ERROR_REPORTING: return "ERROR_REPORTING";
    case CapabilityClass::POISON_REPORTING: return "POISON_REPORTING";
    case CapabilityClass::CAPACITY_RESIZE: return "CAPACITY_RESIZE";
    case CapabilityClass::REGION_RECONFIGURATION: return "REGION_RECONFIGURATION";
    case CapabilityClass::ACCELERATOR_VISIBLE_PATH: return "ACCELERATOR_VISIBLE_PATH";
    case CapabilityClass::CACHE_COHERENT_ACCESS: return "CACHE_COHERENT_ACCESS";
  }
  return "UNKNOWN";
}
inline std::optional<CapabilityClass> from_string(const std::string& s, CapabilityClass*) {
  return enum_lookup<CapabilityClass>(s, {
      {"MEMORY_DEVICE", CapabilityClass::MEMORY_DEVICE},
      {"TYPE3_MEMORY", CapabilityClass::TYPE3_MEMORY},
      {"HOST_MANAGED_DEVICE_MEMORY", CapabilityClass::HOST_MANAGED_DEVICE_MEMORY},
      {"VOLATILE_CAPACITY", CapabilityClass::VOLATILE_CAPACITY},
      {"PERSISTENT_CAPACITY", CapabilityClass::PERSISTENT_CAPACITY},
      {"DYNAMIC_CAPACITY", CapabilityClass::DYNAMIC_CAPACITY},
      {"HOTPLUG", CapabilityClass::HOTPLUG},
      {"MULTI_HOST_ACCESS", CapabilityClass::MULTI_HOST_ACCESS},
      {"MEMORY_POOLING", CapabilityClass::MEMORY_POOLING},
      {"INTERLEAVE", CapabilityClass::INTERLEAVE},
      {"DEVICE_DECODER", CapabilityClass::DEVICE_DECODER},
      {"HOST_DECODER", CapabilityClass::HOST_DECODER},
      {"SWITCH_ATTACHED", CapabilityClass::SWITCH_ATTACHED},
      {"DIRECT_ATTACHED", CapabilityClass::DIRECT_ATTACHED},
      {"ERROR_REPORTING", CapabilityClass::ERROR_REPORTING},
      {"POISON_REPORTING", CapabilityClass::POISON_REPORTING},
      {"CAPACITY_RESIZE", CapabilityClass::CAPACITY_RESIZE},
      {"REGION_RECONFIGURATION", CapabilityClass::REGION_RECONFIGURATION},
      {"ACCELERATOR_VISIBLE_PATH", CapabilityClass::ACCELERATOR_VISIBLE_PATH},
      {"CACHE_COHERENT_ACCESS", CapabilityClass::CACHE_COHERENT_ACCESS}});
}

inline const char* to_string(SupportState v) {
  switch (v) {
    case SupportState::SUPPORTED: return "SUPPORTED";
    case SupportState::UNSUPPORTED: return "UNSUPPORTED";
    case SupportState::UNKNOWN: return "UNKNOWN";
    case SupportState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
  }
  return "UNKNOWN";
}
inline std::optional<SupportState> from_string(const std::string& s, SupportState*) {
  return enum_lookup<SupportState>(s, {
      {"SUPPORTED", SupportState::SUPPORTED}, {"UNSUPPORTED", SupportState::UNSUPPORTED},
      {"UNKNOWN", SupportState::UNKNOWN}, {"REVALIDATION_REQUIRED", SupportState::REVALIDATION_REQUIRED}});
}

inline const char* to_string(EvidenceClass v) {
  switch (v) {
    case EvidenceClass::MEASURED: return "MEASURED";
    case EvidenceClass::PLATFORM_REPORTED: return "PLATFORM_REPORTED";
    case EvidenceClass::CONFIGURED: return "CONFIGURED";
    case EvidenceClass::DERIVED: return "DERIVED";
    case EvidenceClass::SYNTHETIC: return "SYNTHETIC";
    case EvidenceClass::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}
inline std::optional<EvidenceClass> from_string(const std::string& s, EvidenceClass*) {
  return enum_lookup<EvidenceClass>(s, {
      {"MEASURED", EvidenceClass::MEASURED},
      {"PLATFORM_REPORTED", EvidenceClass::PLATFORM_REPORTED},
      {"CONFIGURED", EvidenceClass::CONFIGURED},
      {"DERIVED", EvidenceClass::DERIVED},
      {"SYNTHETIC", EvidenceClass::SYNTHETIC},
      {"UNKNOWN", EvidenceClass::UNKNOWN}});
}

inline const char* to_string(LocalityClass v) {
  switch (v) {
    case LocalityClass::DIRECT_LOCAL: return "DIRECT_LOCAL";
    case LocalityClass::SAME_HOST: return "SAME_HOST";
    case LocalityClass::SAME_NUMA_DOMAIN: return "SAME_NUMA_DOMAIN";
    case LocalityClass::REMOTE_NUMA_DOMAIN: return "REMOTE_NUMA_DOMAIN";
    case LocalityClass::SAME_CXL_ROOT: return "SAME_CXL_ROOT";
    case LocalityClass::SAME_SWITCH: return "SAME_SWITCH";
    case LocalityClass::SWITCH_REMOTE: return "SWITCH_REMOTE";
    case LocalityClass::MULTI_HOP_CXL: return "MULTI_HOP_CXL";
    case LocalityClass::CROSS_HOST: return "CROSS_HOST";
    case LocalityClass::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}
inline std::optional<LocalityClass> from_string(const std::string& s, LocalityClass*) {
  return enum_lookup<LocalityClass>(s, {
      {"DIRECT_LOCAL", LocalityClass::DIRECT_LOCAL}, {"SAME_HOST", LocalityClass::SAME_HOST},
      {"SAME_NUMA_DOMAIN", LocalityClass::SAME_NUMA_DOMAIN},
      {"REMOTE_NUMA_DOMAIN", LocalityClass::REMOTE_NUMA_DOMAIN},
      {"SAME_CXL_ROOT", LocalityClass::SAME_CXL_ROOT}, {"SAME_SWITCH", LocalityClass::SAME_SWITCH},
      {"SWITCH_REMOTE", LocalityClass::SWITCH_REMOTE},
      {"MULTI_HOP_CXL", LocalityClass::MULTI_HOP_CXL}, {"CROSS_HOST", LocalityClass::CROSS_HOST},
      {"UNKNOWN", LocalityClass::UNKNOWN}});
}

inline const char* to_string(TierClass v) {
  switch (v) {
    case TierClass::CXL_NEAR: return "CXL_NEAR";
    case TierClass::CXL_LOCAL: return "CXL_LOCAL";
    case TierClass::CXL_SHARED: return "CXL_SHARED";
    case TierClass::CXL_REMOTE: return "CXL_REMOTE";
    case TierClass::CXL_PERSISTENT: return "CXL_PERSISTENT";
    case TierClass::CXL_CAPACITY: return "CXL_CAPACITY";
    case TierClass::CXL_UNKNOWN: return "CXL_UNKNOWN";
  }
  return "UNKNOWN";
}
inline std::optional<TierClass> from_string(const std::string& s, TierClass*) {
  return enum_lookup<TierClass>(s, {
      {"CXL_NEAR", TierClass::CXL_NEAR}, {"CXL_LOCAL", TierClass::CXL_LOCAL},
      {"CXL_SHARED", TierClass::CXL_SHARED}, {"CXL_REMOTE", TierClass::CXL_REMOTE},
      {"CXL_PERSISTENT", TierClass::CXL_PERSISTENT}, {"CXL_CAPACITY", TierClass::CXL_CAPACITY},
      {"CXL_UNKNOWN", TierClass::CXL_UNKNOWN}});
}

inline const char* to_string(AcceleratorAccessState v) {
  switch (v) {
    case AcceleratorAccessState::DIRECT_SUPPORTED: return "DIRECT_SUPPORTED";
    case AcceleratorAccessState::INDIRECT_SUPPORTED: return "INDIRECT_SUPPORTED";
    case AcceleratorAccessState::HOST_MEDIATED: return "HOST_MEDIATED";
    case AcceleratorAccessState::UNSUPPORTED: return "UNSUPPORTED";
    case AcceleratorAccessState::UNKNOWN: return "UNKNOWN";
    case AcceleratorAccessState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
  }
  return "UNKNOWN";
}
inline std::optional<AcceleratorAccessState> from_string(const std::string& s, AcceleratorAccessState*) {
  return enum_lookup<AcceleratorAccessState>(s, {
      {"DIRECT_SUPPORTED", AcceleratorAccessState::DIRECT_SUPPORTED},
      {"INDIRECT_SUPPORTED", AcceleratorAccessState::INDIRECT_SUPPORTED},
      {"HOST_MEDIATED", AcceleratorAccessState::HOST_MEDIATED},
      {"UNSUPPORTED", AcceleratorAccessState::UNSUPPORTED},
      {"UNKNOWN", AcceleratorAccessState::UNKNOWN},
      {"REVALIDATION_REQUIRED", AcceleratorAccessState::REVALIDATION_REQUIRED}});
}

inline const char* to_string(ResumeState v) {
  switch (v) {
    case ResumeState::RESERVED: return "RESERVED";
    case ResumeState::COMMITTED: return "COMMITTED";
    case ResumeState::RELEASED: return "RELEASED";
    case ResumeState::FENCED: return "FENCED";
  }
  return "UNKNOWN";
}
inline std::optional<ResumeState> from_string(const std::string& s, ResumeState*) {
  return enum_lookup<ResumeState>(s, {
      {"RESERVED", ResumeState::RESERVED}, {"COMMITTED", ResumeState::COMMITTED},
      {"RELEASED", ResumeState::RELEASED}, {"FENCED", ResumeState::FENCED}});
}

inline const char* to_string(VolatilityKind v) {
  switch (v) {
    case VolatilityKind::VOLATILE: return "VOLATILE";
    case VolatilityKind::PERSISTENT: return "PERSISTENT";
    case VolatilityKind::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}
inline std::optional<VolatilityKind> from_string(const std::string& s, VolatilityKind*) {
  return enum_lookup<VolatilityKind>(s, {
      {"VOLATILE", VolatilityKind::VOLATILE}, {"PERSISTENT", VolatilityKind::PERSISTENT},
      {"UNKNOWN", VolatilityKind::UNKNOWN}});
}

inline const char* to_string(SharingMode v) {
  switch (v) {
    case SharingMode::DEDICATED: return "DEDICATED";
    case SharingMode::SHARED: return "SHARED";
    case SharingMode::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}
inline std::optional<SharingMode> from_string(const std::string& s, SharingMode*) {
  return enum_lookup<SharingMode>(s, {
      {"DEDICATED", SharingMode::DEDICATED}, {"SHARED", SharingMode::SHARED},
      {"UNKNOWN", SharingMode::UNKNOWN}});
}

inline const char* to_string(FailoverOutcome v) {
  switch (v) {
    case FailoverOutcome::KEEP: return "KEEP";
    case FailoverOutcome::DRAIN: return "DRAIN";
    case FailoverOutcome::FAILOVER: return "FAILOVER";
    case FailoverOutcome::REVALIDATE: return "REVALIDATE";
    case FailoverOutcome::REJECT: return "REJECT";
    case FailoverOutcome::DEGRADED: return "DEGRADED";
    case FailoverOutcome::LOST: return "LOST";
  }
  return "UNKNOWN";
}
inline std::optional<FailoverOutcome> from_string(const std::string& s, FailoverOutcome*) {
  return enum_lookup<FailoverOutcome>(s, {
      {"KEEP", FailoverOutcome::KEEP}, {"DRAIN", FailoverOutcome::DRAIN},
      {"FAILOVER", FailoverOutcome::FAILOVER}, {"REVALIDATE", FailoverOutcome::REVALIDATE},
      {"REJECT", FailoverOutcome::REJECT}, {"DEGRADED", FailoverOutcome::DEGRADED},
      {"LOST", FailoverOutcome::LOST}});
}

inline const char* to_string(FailureDomainLevel v) {
  switch (v) {
    case FailureDomainLevel::NONE: return "NONE";
    case FailureDomainLevel::DEVICE: return "DEVICE";
    case FailureDomainLevel::PORT: return "PORT";
    case FailureDomainLevel::SWITCH: return "SWITCH";
    case FailureDomainLevel::ROOT_COMPLEX: return "ROOT_COMPLEX";
    case FailureDomainLevel::HOST: return "HOST";
    case FailureDomainLevel::RACK: return "RACK";
  }
  return "UNKNOWN";
}
inline std::optional<FailureDomainLevel> from_string(const std::string& s, FailureDomainLevel*) {
  return enum_lookup<FailureDomainLevel>(s, {
      {"NONE", FailureDomainLevel::NONE}, {"DEVICE", FailureDomainLevel::DEVICE},
      {"PORT", FailureDomainLevel::PORT}, {"SWITCH", FailureDomainLevel::SWITCH},
      {"ROOT_COMPLEX", FailureDomainLevel::ROOT_COMPLEX},
      {"HOST", FailureDomainLevel::HOST}, {"RACK", FailureDomainLevel::RACK}});
}

inline const char* to_string(DecisionReason v) {
  switch (v) {
    case DecisionReason::OK: return "OK";
    case DecisionReason::DEVICE_STALE: return "DEVICE_STALE";
    case DecisionReason::REGION_STALE: return "REGION_STALE";
    case DecisionReason::POOL_STALE: return "POOL_STALE";
    case DecisionReason::DEVICE_NOT_ONLINE: return "DEVICE_NOT_ONLINE";
    case DecisionReason::REGION_NOT_ONLINE: return "REGION_NOT_ONLINE";
    case DecisionReason::EVIDENCE_STALE: return "EVIDENCE_STALE";
    case DecisionReason::CAPABILITY_UNSUPPORTED: return "CAPABILITY_UNSUPPORTED";
    case DecisionReason::CAPABILITY_UNKNOWN: return "CAPABILITY_UNKNOWN";
    case DecisionReason::CAPACITY_INSUFFICIENT: return "CAPACITY_INSUFFICIENT";
    case DecisionReason::VOLATILITY_MISMATCH: return "VOLATILITY_MISMATCH";
    case DecisionReason::HOST_INCOMPATIBLE: return "HOST_INCOMPATIBLE";
    case DecisionReason::SHARING_MISMATCH: return "SHARING_MISMATCH";
    case DecisionReason::REQUIRED_LOCALITY_UNSATISFIED: return "REQUIRED_LOCALITY_UNSATISFIED";
    case DecisionReason::ACCELERATOR_ACCESS_UNSATISFIED: return "ACCELERATOR_ACCESS_UNSATISFIED";
    case DecisionReason::HEALTH_UNACCEPTABLE: return "HEALTH_UNACCEPTABLE";
    case DecisionReason::DRAINING: return "DRAINING";
    case DecisionReason::FAILED: return "FAILED";
    case DecisionReason::POLICY_DENIED: return "POLICY_DENIED";
    case DecisionReason::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    case DecisionReason::POOL_GENERATION_STALE: return "POOL_GENERATION_STALE";
    case DecisionReason::RESERVATION_STALE: return "RESERVATION_STALE";
    case DecisionReason::DUPLICATE_IDENTITY: return "DUPLICATE_IDENTITY";
    case DecisionReason::CONFLICTING_FACTS: return "CONFLICTING_FACTS";
    case DecisionReason::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case DecisionReason::RESERVATION_CONFLICT: return "RESERVATION_CONFLICT";
    case DecisionReason::CAPACITY_OVERCOMMIT: return "CAPACITY_OVERCOMMIT";
    case DecisionReason::PERSISTENCE_CORRUPT: return "PERSISTENCE_CORRUPT";
    case DecisionReason::PROTOCOL_MALFORMED: return "PROTOCOL_MALFORMED";
    case DecisionReason::PROTOCOL_OVERSIZED: return "PROTOCOL_OVERSIZED";
    case DecisionReason::PROTOCOL_INVALID_ENUM: return "PROTOCOL_INVALID_ENUM";
    case DecisionReason::PROTOCOL_INVALID_GENERATION: return "PROTOCOL_INVALID_GENERATION";
    case DecisionReason::PROTOCOL_TRAILING_GARBAGE: return "PROTOCOL_TRAILING_GARBAGE";
    case DecisionReason::UNKNOWN_ERROR: return "UNKNOWN_ERROR";
  }
  return "UNKNOWN";
}
inline std::optional<DecisionReason> from_string(const std::string& s, DecisionReason*) {
  return enum_lookup<DecisionReason>(s, {
      {"OK", DecisionReason::OK}, {"DEVICE_STALE", DecisionReason::DEVICE_STALE},
      {"REGION_STALE", DecisionReason::REGION_STALE}, {"POOL_STALE", DecisionReason::POOL_STALE},
      {"DEVICE_NOT_ONLINE", DecisionReason::DEVICE_NOT_ONLINE},
      {"REGION_NOT_ONLINE", DecisionReason::REGION_NOT_ONLINE},
      {"EVIDENCE_STALE", DecisionReason::EVIDENCE_STALE},
      {"CAPABILITY_UNSUPPORTED", DecisionReason::CAPABILITY_UNSUPPORTED},
      {"CAPABILITY_UNKNOWN", DecisionReason::CAPABILITY_UNKNOWN},
      {"CAPACITY_INSUFFICIENT", DecisionReason::CAPACITY_INSUFFICIENT},
      {"VOLATILITY_MISMATCH", DecisionReason::VOLATILITY_MISMATCH},
      {"HOST_INCOMPATIBLE", DecisionReason::HOST_INCOMPATIBLE},
      {"SHARING_MISMATCH", DecisionReason::SHARING_MISMATCH},
      {"REQUIRED_LOCALITY_UNSATISFIED", DecisionReason::REQUIRED_LOCALITY_UNSATISFIED},
      {"ACCELERATOR_ACCESS_UNSATISFIED", DecisionReason::ACCELERATOR_ACCESS_UNSATISFIED},
      {"HEALTH_UNACCEPTABLE", DecisionReason::HEALTH_UNACCEPTABLE},
      {"DRAINING", DecisionReason::DRAINING}, {"FAILED", DecisionReason::FAILED},
      {"POLICY_DENIED", DecisionReason::POLICY_DENIED},
      {"REVALIDATION_REQUIRED", DecisionReason::REVALIDATION_REQUIRED},
      {"POOL_GENERATION_STALE", DecisionReason::POOL_GENERATION_STALE},
      {"RESERVATION_STALE", DecisionReason::RESERVATION_STALE},
      {"DUPLICATE_IDENTITY", DecisionReason::DUPLICATE_IDENTITY},
      {"CONFLICTING_FACTS", DecisionReason::CONFLICTING_FACTS},
      {"INVALID_ARGUMENT", DecisionReason::INVALID_ARGUMENT},
      {"RESERVATION_CONFLICT", DecisionReason::RESERVATION_CONFLICT},
      {"CAPACITY_OVERCOMMIT", DecisionReason::CAPACITY_OVERCOMMIT},
      {"PERSISTENCE_CORRUPT", DecisionReason::PERSISTENCE_CORRUPT},
      {"PROTOCOL_MALFORMED", DecisionReason::PROTOCOL_MALFORMED},
      {"PROTOCOL_OVERSIZED", DecisionReason::PROTOCOL_OVERSIZED},
      {"PROTOCOL_INVALID_ENUM", DecisionReason::PROTOCOL_INVALID_ENUM},
      {"PROTOCOL_INVALID_GENERATION", DecisionReason::PROTOCOL_INVALID_GENERATION},
      {"PROTOCOL_TRAILING_GARBAGE", DecisionReason::PROTOCOL_TRAILING_GARBAGE},
      {"UNKNOWN_ERROR", DecisionReason::UNKNOWN_ERROR}});
}

}  // namespace cxl_fabric
