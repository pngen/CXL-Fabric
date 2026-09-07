// ============================================================================
// CXL Fabric - governed reservation and admission model
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/accounting.hpp"
#include "cxl_fabric/core/enums.hpp"
#include "cxl_fabric/core/identity.hpp"
#include <optional>
#include <string>

namespace cxl_fabric {

// Constraints a consumer places on a CXL-capacity admission request. A policy
// decision (evaluate) is not a reservation, and a reservation is not committed
// use. The three stages stay distinct.
struct AdmissionRequest {
  ByteCount bytes = 0;
  VolatilityKind volatility = VolatilityKind::UNKNOWN;
  ConsumerId consumer;
  HostId host;
  std::optional<PoolId> required_pool;
  std::optional<LocalityClass> minimum_locality;   // hard if set
  std::optional<TierClass> required_tier;
  std::optional<AcceleratorAccessState> accelerator_access;  // hard if set
  std::optional<FailureDomainLevel> failure_domain_minimum;  // prefer if set
  SharingMode sharing = SharingMode::UNKNOWN;
  bool require_unique_failure_domain = false;
  bool require_persistence = false;
  bool require_accelerator_path = false;
  bool prefer_fallback = false;       // prefer best, allow fallback
  std::string policy_class;
  std::uint64_t minimum_bandwidth_mbps = 0;         // hard if non-zero
  std::uint64_t maximum_latency_ns = 0;             // hard if non-zero
};

// A live reservation. Generation-bound: the record captures exactly the
// generations that authorized it so a stale commit/release is fenced.
struct Reservation {
  ReservationId id;
  ConsumerId consumer;
  ConsumerGeneration consumer_generation;
  PoolId pool;
  PoolGeneration pool_generation;
  RegionId region;
  RegionGeneration region_generation;
  DeviceId device;
  DeviceGeneration device_generation;
  DeviceBootId device_boot_id;
  ByteCount bytes = 0;
  ResumeState state = ResumeState::RESERVED;
  CoordinatorEpoch created_epoch;      // epoch at reservation
  CoordinatorEpoch commit_epoch;       // epoch at commit
  WorkerBootId worker_boot_id;         // worker that created the reservation
  bool evidence_current_at_reserve = false;
  bool failed_device_entry = true;     // whether the device is still present
};

}  // namespace cxl_fabric
