// ============================================================================
// CXL Fabric - explicit CXL capability model
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/enums.hpp"
#include "cxl_fabric/core/identity.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cxl_fabric {

// A single capability assertion. UNKNOWN must fail closed for hard requirements.
struct Capability {
  CapabilityClass klass = CapabilityClass::MEMORY_DEVICE;
  SupportState state = SupportState::UNKNOWN;
  std::uint64_t version = 0;
  Provenance provenance = Provenance::UNSUPPORTED;
  EvidenceGeneration evidence_generation;
  bool evidence_current = false;
  std::string constraints;  // optional textual constraint notes

  bool is_supported() const noexcept { return state == SupportState::SUPPORTED; }
  bool is_unknown() const noexcept { return state == SupportState::UNKNOWN; }
  bool requires_revalidation() const noexcept {
    return state == SupportState::REVALIDATION_REQUIRED;
  }
};

// A collection of capability facts for a device/region/pool.
class Capabilities {
public:
  void set(Capability cap) { map_[cap.klass] = std::move(cap); }
  std::optional<Capability> get(CapabilityClass k) const {
    auto it = map_.find(k);
    if (it == map_.end()) return std::nullopt;
    return it->second;
  }
  bool has(CapabilityClass k) const { return map_.count(k) != 0; }
  const std::map<CapabilityClass, Capability>& all() const { return map_; }
  // Required capability is supported AND evidence current.
  bool required_supported(CapabilityClass k, bool require_current_evidence = true) const {
    auto c = get(k);
    if (!c) return false;
    if (c->state != SupportState::SUPPORTED) return false;
    if (require_current_evidence && !c->evidence_current) return false;
    return true;
  }
  size_t size() const { return map_.size(); }
private:
  std::map<CapabilityClass, Capability> map_;
};

}  // namespace cxl_fabric
