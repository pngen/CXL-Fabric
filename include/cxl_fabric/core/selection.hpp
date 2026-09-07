// ============================================================================
// CXL Fabric - deterministic candidate evaluation and selection
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/device.hpp"
#include "cxl_fabric/core/economics.hpp"
#include "cxl_fabric/core/enums.hpp"
#include "cxl_fabric/core/enum_strings.hpp"
#include "cxl_fabric/core/identity.hpp"
#include "cxl_fabric/core/pool.hpp"
#include "cxl_fabric/core/region.hpp"
#include "cxl_fabric/core/reservation.hpp"
#include <optional>
#include <string>
#include <vector>

namespace cxl_fabric {

// A candidate is a generation-bound tuple of pool/region/device plus their
// current ability to provision capacity.
struct Candidate {
  PoolId pool;
  PoolGeneration pool_generation;
  RegionId region;
  RegionGeneration region_generation;
  DeviceId device;
  DeviceGeneration device_generation;
  ByteCount available_bytes = 0;
  const CxlDevice* device_record = nullptr;
  const CxlRegion* region_record = nullptr;
  const CxlPool* pool_record = nullptr;
};

struct SelectionFactor {
  std::string name;
  bool pass = true;
  std::string detail;
};

// Structured, explainable outcome. An opaque score is never produced; each
// rejection carries an explicit reason code and each pass a named factor.
struct SelectionResult {
  bool selected = false;
  DecisionReason reason = DecisionReason::OK;
  std::string pool;
  std::string region;
  std::string device;
  std::string locality;
  ByteCount free_bytes = 0;
  HealthState health = HealthState::UNKNOWN;
  bool evidence_current = false;
  std::vector<DecisionReason> rejections;
  std::vector<SelectionFactor> factors;
  std::string explanation;
};

inline bool locality_satisfies(LocalityClass have, LocalityClass need) {
  auto ord = [](LocalityClass l) -> int {
    switch (l) {
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
  };
  return ord(have) <= ord(need);
}

// Hard constraint check. UNKNOWN fails closed for hard requirements.
inline SelectionResult evaluate_candidate(const Candidate& c,
                                          const AdmissionRequest& req,
                                          bool pool_evidence_current) {
  SelectionResult r;
  r.selected = true;
  r.factors.push_back({"pool", true, c.pool.str()});
  r.factors.push_back({"region", true, c.region.str()});
  r.factors.push_back({"device", true, c.device.str()});

  // Candidate references must be complete.
  if (!c.device_record || !c.region_record || !c.pool_record) {
    r.selected = false; r.reason = DecisionReason::UNKNOWN_ERROR;
    r.rejections.push_back(DecisionReason::UNKNOWN_ERROR);
    return r;
  }
  const CxlDevice& dev = *c.device_record;
  const CxlRegion& reg = *c.region_record;
  const CxlPool& pool = *c.pool_record;

  // --- Hard constraints, in deterministic order ---
  // Current evidence.
  if (!dev.evidence_current || !reg.evidence_current || !pool_evidence_current) {
    r.selected = false; r.reason = DecisionReason::EVIDENCE_STALE;
    r.rejections.push_back(DecisionReason::EVIDENCE_STALE);
    r.factors.push_back({"evidence_current", false, "stale"});
    return r;
  }
  // Device / region / pool online.
  if (dev.state != DeviceState::ONLINE) {
    r.selected = false; r.reason = DecisionReason::DEVICE_NOT_ONLINE;
    r.rejections.push_back(DecisionReason::DEVICE_NOT_ONLINE);
    r.factors.push_back({"device_state", false, to_string(dev.state)});
    return r;
  }
  if (reg.state != RegionState::ONLINE) {
    r.selected = false; r.reason = DecisionReason::REGION_NOT_ONLINE;
    r.rejections.push_back(DecisionReason::REGION_NOT_ONLINE);
    r.factors.push_back({"region_state", false, to_string(reg.state)});
    return r;
  }
  if (pool.state != PoolState::AVAILABLE && pool.state != PoolState::DEFINED) {
    r.selected = false; r.reason = DecisionReason::POOL_STALE;
    r.rejections.push_back(DecisionReason::POOL_STALE);
    r.factors.push_back({"pool_state", false, to_string(pool.state)});
    return r;
  }
  // Draining / failed / revalidation.
  if (dev.state == DeviceState::DRAINING) {
    r.selected = false; r.reason = DecisionReason::DRAINING;
    r.rejections.push_back(DecisionReason::DRAINING);
    r.factors.push_back({"device_draining", false, "true"});
    return r;
  }
  if (dev.state == DeviceState::FAILED || reg.state == RegionState::FAILED) {
    r.selected = false; r.reason = DecisionReason::FAILED;
    r.rejections.push_back(DecisionReason::FAILED);
    r.factors.push_back({"device_failed", false, "true"});
    return r;
  }
  if (dev.state == DeviceState::REVALIDATION_REQUIRED ||
      reg.state == RegionState::REVALIDATION_REQUIRED ||
      pool.state == PoolState::REVALIDATION_REQUIRED) {
    r.selected = false; r.reason = DecisionReason::REVALIDATION_REQUIRED;
    r.rejections.push_back(DecisionReason::REVALIDATION_REQUIRED);
    r.factors.push_back({"revalidation_required", false, "true"});
    return r;
  }
  // Health.
  if (dev.health == HealthState::FAILED || dev.health == HealthState::POISONED ||
      reg.health == HealthState::FAILED || reg.health == HealthState::POISONED ||
      pool.health_summary == HealthState::FAILED || pool.health_summary == HealthState::POISONED) {
    r.selected = false; r.reason = DecisionReason::HEALTH_UNACCEPTABLE;
    r.rejections.push_back(DecisionReason::HEALTH_UNACCEPTABLE);
    r.factors.push_back({"health", false, to_string(dev.health)});
    return r;
  }
  // Capacity.
  if (c.available_bytes < req.bytes) {
    r.selected = false; r.reason = DecisionReason::CAPACITY_INSUFFICIENT;
    r.rejections.push_back(DecisionReason::CAPACITY_INSUFFICIENT);
    r.factors.push_back({"capacity", false, std::to_string(c.available_bytes)});
    return r;
  }
  // Volatility.
  if (req.require_persistence && dev.volatility != VolatilityKind::PERSISTENT) {
    r.selected = false; r.reason = DecisionReason::VOLATILITY_MISMATCH;
    r.rejections.push_back(DecisionReason::VOLATILITY_MISMATCH);
    r.factors.push_back({"volatility", false, to_string(dev.volatility)});
    return r;
  }
  if (req.volatility != VolatilityKind::UNKNOWN && dev.volatility != VolatilityKind::UNKNOWN &&
      req.volatility != dev.volatility) {
    r.selected = false; r.reason = DecisionReason::VOLATILITY_MISMATCH;
    r.rejections.push_back(DecisionReason::VOLATILITY_MISMATCH);
    r.factors.push_back({"volatility", false, to_string(dev.volatility)});
    return r;
  }
  // Host compatibility.
  if (!req.host.empty() && !dev.host.empty() && req.host != dev.host) {
    r.selected = false; r.reason = DecisionReason::HOST_INCOMPATIBLE;
    r.rejections.push_back(DecisionReason::HOST_INCOMPATIBLE);
    r.factors.push_back({"host", false, dev.host.str()});
    return r;
  }
  // Sharing.
  if (req.sharing != SharingMode::UNKNOWN && dev.sharing != SharingMode::UNKNOWN &&
      req.sharing != dev.sharing) {
    r.selected = false; r.reason = DecisionReason::SHARING_MISMATCH;
    r.rejections.push_back(DecisionReason::SHARING_MISMATCH);
    r.factors.push_back({"sharing", false, to_string(dev.sharing)});
    return r;
  }
  // Pool requirement.
  if (req.required_pool && *req.required_pool != pool.id) {
    r.selected = false; r.reason = DecisionReason::POOL_STALE;
    r.rejections.push_back(DecisionReason::POOL_STALE);
    r.factors.push_back({"required_pool", false, pool.id.str()});
    return r;
  }
  // Locality.
  if (req.minimum_locality && !locality_satisfies(dev.economics.locality, *req.minimum_locality)) {
    r.selected = false; r.reason = DecisionReason::REQUIRED_LOCALITY_UNSATISFIED;
    r.rejections.push_back(DecisionReason::REQUIRED_LOCALITY_UNSATISFIED);
    r.factors.push_back({"locality", false, to_string(dev.economics.locality)});
    return r;
  }
  // Tier.
  if (req.required_tier && reg.tier != *req.required_tier) {
    r.selected = false; r.reason = DecisionReason::POLICY_DENIED;
    r.rejections.push_back(DecisionReason::POLICY_DENIED);
    r.factors.push_back({"tier", false, to_string(reg.tier)});
    return r;
  }
  // Accelerator access.
  if (req.accelerator_access) {
    AcceleratorAccessState need = *req.accelerator_access;
    // For hard requirement, DIRECT_SUPPORTED or INDIRECT_SUPPORTED are the only
    // acceptable; HOST_MEDIATED is not direct/indirect.
    if (need != dev.accelerator_access) {
      r.selected = false; r.reason = DecisionReason::ACCELERATOR_ACCESS_UNSATISFIED;
      r.rejections.push_back(DecisionReason::ACCELERATOR_ACCESS_UNSATISFIED);
      r.factors.push_back({"accelerator_access", false, to_string(dev.accelerator_access)});
      return r;
    }
  }
  // Bandwidth / latency.
  if (req.minimum_bandwidth_mbps > 0 && dev.economics.estimated_bandwidth_mbps != 0 &&
      dev.economics.estimated_bandwidth_mbps < req.minimum_bandwidth_mbps) {
    r.selected = false; r.reason = DecisionReason::POLICY_DENIED;
    r.rejections.push_back(DecisionReason::POLICY_DENIED);
    r.factors.push_back({"bandwidth", false, std::to_string(dev.economics.estimated_bandwidth_mbps)});
    return r;
  }
  if (req.maximum_latency_ns > 0 && dev.economics.estimated_latency_ns != 0 &&
      dev.economics.estimated_latency_ns > req.maximum_latency_ns) {
    r.selected = false; r.reason = DecisionReason::POLICY_DENIED;
    r.rejections.push_back(DecisionReason::POLICY_DENIED);
    r.factors.push_back({"latency", false, std::to_string(dev.economics.estimated_latency_ns)});
    return r;
  }

  // --- Passed hardness. Populate outcome and ranking factors. ---
  r.selected = true;
  r.reason = DecisionReason::OK;
  r.pool = pool.id.str();
  r.region = reg.id.str();
  r.device = dev.id.str();
  r.locality = to_string(dev.economics.locality);
  r.free_bytes = c.available_bytes;
  r.health = dev.health;
  r.evidence_current = dev.evidence_current;
  r.factors.push_back({"locality", true, to_string(dev.economics.locality)});
  r.factors.push_back({"capacity", true, std::to_string(c.available_bytes)});
  r.factors.push_back({"failure_domain", true, dev.economics.notes.empty() ? "n/a" : dev.economics.notes});
  if (dev.economics.estimated_latency_ns > 0)
    r.factors.push_back({"latency", true, std::to_string(dev.economics.estimated_latency_ns) + "ns"});
  if (dev.economics.estimated_bandwidth_mbps > 0)
    r.factors.push_back({"bandwidth", true, std::to_string(dev.economics.estimated_bandwidth_mbps) + "MB/s"});
  return r;
}

}  // namespace cxl_fabric
