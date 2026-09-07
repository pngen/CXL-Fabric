// ============================================================================
// CXL Fabric - validated lifecycle state transitions
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/enums.hpp"

namespace cxl_fabric {

// Device lifecycle. No silent jumps: every transition is validated.
inline bool valid_transition(DeviceState from, DeviceState to) {
  if (from == to) return true;
  switch (from) {
    case DeviceState::DISCOVERED:          return to==DeviceState::ENUMERATED;
    case DeviceState::ENUMERATED:          return to==DeviceState::AVAILABLE || to==DeviceState::DISCOVERED || to==DeviceState::OFFLINE || to==DeviceState::FAILED || to==DeviceState::RETIRED;
    case DeviceState::AVAILABLE:           return to==DeviceState::ONLINE || to==DeviceState::DEGRADED || to==DeviceState::DRAINING || to==DeviceState::REVALIDATION_REQUIRED || to==DeviceState::OFFLINE || to==DeviceState::FAILED || to==DeviceState::REMOVED || to==DeviceState::RETIRED;
    case DeviceState::ONLINE:              return to==DeviceState::ONLINE || to==DeviceState::DEGRADED || to==DeviceState::DRAINING || to==DeviceState::REVALIDATION_REQUIRED || to==DeviceState::OFFLINE || to==DeviceState::FAILED || to==DeviceState::REMOVED || to==DeviceState::RETIRED;
    case DeviceState::DEGRADED:            return to==DeviceState::ONLINE || to==DeviceState::AVAILABLE || to==DeviceState::DRAINING || to==DeviceState::REVALIDATION_REQUIRED || to==DeviceState::OFFLINE || to==DeviceState::FAILED || to==DeviceState::REMOVED || to==DeviceState::RETIRED;
    case DeviceState::DRAINING:            return to==DeviceState::ONLINE || to==DeviceState::AVAILABLE || to==DeviceState::REVALIDATION_REQUIRED || to==DeviceState::OFFLINE || to==DeviceState::FAILED || to==DeviceState::REMOVED || to==DeviceState::RETIRED;
    case DeviceState::REVALIDATION_REQUIRED: return to==DeviceState::ONLINE || to==DeviceState::AVAILABLE || to==DeviceState::REVALIDATION_REQUIRED || to==DeviceState::OFFLINE || to==DeviceState::FAILED || to==DeviceState::REMOVED || to==DeviceState::RETIRED;
    case DeviceState::OFFLINE:             return to==DeviceState::ONLINE || to==DeviceState::AVAILABLE || to==DeviceState::REVALIDATION_REQUIRED || to==DeviceState::REMOVED || to==DeviceState::RETIRED;
    case DeviceState::FAILED:              return to==DeviceState::REMOVED || to==DeviceState::RETIRED;
    case DeviceState::REMOVED:             return to==DeviceState::RETIRED;
    case DeviceState::RETIRED:             return false;
  }
  return false;
}

inline bool valid_transition(RegionState from, RegionState to) {
  if (from == to) return true;
  switch (from) {
    case RegionState::DEFINED:           return to==RegionState::VALIDATING || to==RegionState::RETIRED;
    case RegionState::VALIDATING:        return to==RegionState::ONLINE || to==RegionState::DEFINED || to==RegionState::FAILED || to==RegionState::RETIRED;
    case RegionState::ONLINE:            return to==RegionState::DEGRADED || to==RegionState::DRAINING || to==RegionState::RECONFIGURING || to==RegionState::REVALIDATION_REQUIRED || to==RegionState::OFFLINE || to==RegionState::FAILED || to==RegionState::RETIRED;
    case RegionState::DEGRADED:          return to==RegionState::ONLINE || to==RegionState::DRAINING || to==RegionState::RECONFIGURING || to==RegionState::REVALIDATION_REQUIRED || to==RegionState::OFFLINE || to==RegionState::FAILED || to==RegionState::RETIRED;
    case RegionState::DRAINING:          return to==RegionState::ONLINE || to==RegionState::REVALIDATION_REQUIRED || to==RegionState::OFFLINE || to==RegionState::FAILED || to==RegionState::RETIRED;
    case RegionState::RECONFIGURING:     return to==RegionState::ONLINE || to==RegionState::VALIDATING || to==RegionState::REVALIDATION_REQUIRED || to==RegionState::FAILED || to==RegionState::RETIRED;
    case RegionState::REVALIDATION_REQUIRED: return to==RegionState::ONLINE || to==RegionState::VALIDATING || to==RegionState::DRAINING || to==RegionState::OFFLINE || to==RegionState::FAILED || to==RegionState::RETIRED;
    case RegionState::OFFLINE:           return to==RegionState::ONLINE || to==RegionState::VALIDATING || to==RegionState::REVALIDATION_REQUIRED || to==RegionState::RETIRED;
    case RegionState::FAILED:            return to==RegionState::RETIRED;
    case RegionState::RETIRED:           return false;
  }
  return false;
}

inline bool valid_transition(PoolState from, PoolState to) {
  if (from == to) return true;
  switch (from) {
    case PoolState::DEFINED:             return to==PoolState::AVAILABLE || to==PoolState::FAILED || to==PoolState::RETIRED;
    case PoolState::AVAILABLE:           return to==PoolState::DEGRADED || to==PoolState::DRAINING || to==PoolState::REVALIDATION_REQUIRED || to==PoolState::FAILED || to==PoolState::RETIRED;
    case PoolState::DEGRADED:            return to==PoolState::AVAILABLE || to==PoolState::DRAINING || to==PoolState::REVALIDATION_REQUIRED || to==PoolState::FAILED || to==PoolState::RETIRED;
    case PoolState::DRAINING:            return to==PoolState::AVAILABLE || to==PoolState::REVALIDATION_REQUIRED || to==PoolState::FAILED || to==PoolState::RETIRED;
    case PoolState::REVALIDATION_REQUIRED: return to==PoolState::AVAILABLE || to==PoolState::DEGRADED || to==PoolState::DRAINING || to==PoolState::FAILED || to==PoolState::RETIRED;
    case PoolState::FAILED:              return to==PoolState::RETIRED;
    case PoolState::RETIRED:             return false;
  }
  return false;
}

}  // namespace cxl_fabric
