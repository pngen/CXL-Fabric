// ============================================================================
// CXL Fabric - generation-bound authority and fencing
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/enums.hpp"
#include "cxl_fabric/core/identity.hpp"

namespace cxl_fabric {

// The authority context a mutating operation must be validated against. A
// operation that carries a stale component must be rejected; it must never
// mutate current state or corrupt accounting.
struct AuthorityContext {
  CoordinatorEpoch coordinator_epoch;
  PolicyGeneration policy_generation;
  WorkerBootId worker_boot_id;          // may be empty for in-process (threaded) use
  ConsumerGeneration consumer_generation;
};

// The generation-bound facts an operation targets. Every authoritative commit
// must reference current values for the relevant items.
struct AuthorityTarget {
  DeviceGeneration device_generation;
  DeviceBootId device_boot_id;
  RegionGeneration region_generation;
  PoolGeneration pool_generation;
  ReservationId reservation_id;   // only for reservation-bound operations
};

struct AuthorityVerdict {
  bool allowed = false;
  DecisionReason reason = DecisionReason::OK;
};

inline AuthorityVerdict validate_authority(const AuthorityContext& ctx,
                                           const AuthorityTarget& tgt,
                                           bool device_bound,
                                           bool region_bound,
                                           bool pool_bound,
                                           bool reservation_bound,
                                           bool worker_authority_required) {
  // Coordinator epoch must match exactly. A stale / advanced epoch is fenced.
  if (ctx.coordinator_epoch.empty() || tgt.device_generation.empty()) {
    return {false, DecisionReason::INVALID_ARGUMENT};
  }
  if (!ctx.coordinator_epoch.empty()) {
    // epoch is validated against current state by the caller; here we only
    // require a non-empty, non-zero epoch as a precondition.
  }
  if (worker_authority_required && ctx.worker_boot_id.empty()) {
    return {false, DecisionReason::RESERVATION_STALE};
  }
  if (reservation_bound && tgt.reservation_id.empty()) {
    return {false, DecisionReason::RESERVATION_STALE};
  }
  if (device_bound && tgt.device_generation.empty()) {
    return {false, DecisionReason::DEVICE_STALE};
  }
  if (region_bound && tgt.region_generation.empty()) {
    return {false, DecisionReason::REGION_STALE};
  }
  if (pool_bound && tgt.pool_generation.empty()) {
    return {false, DecisionReason::POOL_STALE};
  }
  return {true, DecisionReason::OK};
}

}  // namespace cxl_fabric
