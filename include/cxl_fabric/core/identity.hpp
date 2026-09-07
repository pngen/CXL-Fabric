// ============================================================================
// CXL Fabric - typed identity and generation model
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace cxl_fabric {

// ---------------------------------------------------------------------------
// Typed string identity. Distinct CXL entities never share an interchangeable
// integer type; each identity carries its own tag so that (for example) a
// DeviceId cannot be silently passed where a RegionId is expected.
// ---------------------------------------------------------------------------
template <class Tag>
class Id {
public:
  using TagType = Tag;

  Id() = default;
  explicit Id(std::string v) : value_(std::move(v)) {}

  // A valid, usable identity is never empty.
  bool empty() const noexcept { return value_.empty(); }
  explicit operator bool() const noexcept { return !value_.empty(); }

  const std::string& value() const noexcept { return value_; }
  std::string str() const { return value_; }

  friend bool operator==(const Id& a, const Id& b) noexcept { return a.value_ == b.value_; }
  friend bool operator!=(const Id& a, const Id& b) noexcept { return a.value_ != b.value_; }
  friend bool operator<(const Id& a, const Id& b) noexcept { return a.value_ < b.value_; }

private:
  std::string value_;
};

// ---------------------------------------------------------------------------
// Typed monotonically increasing generation. Zero is the invalid "empty"
// generation; every real generation is 1..MAX. Arithmetic is checked and never
// silently wraps to zero (which would re-authorize stale state).
// ---------------------------------------------------------------------------
template <class Tag>
class Generation {
public:
  using TagType = Tag;

  Generation() = default;
  explicit Generation(std::uint64_t v) : value_(v) {}

  bool empty() const noexcept { return value_ == 0; }
  explicit operator bool() const noexcept { return value_ != 0; }
  std::uint64_t value() const noexcept { return value_; }

  // Next generation. Returns 0 (invalid) if the maximum is reached so that
  // overflow can be detected rather than silently wrapping to a reused value.
  Generation next() const noexcept {
    if (value_ == max()) { return Generation{}; }
    return Generation{value_ + 1};
  }

  bool operator==(const Generation& o) const noexcept { return value_ == o.value_; }
  bool operator!=(const Generation& o) const noexcept { return value_ != o.value_; }
  bool operator<(const Generation& o) const noexcept { return value_ < o.value_; }
  bool operator<=(const Generation& o) const noexcept { return value_ <= o.value_; }
  bool operator>(const Generation& o) const noexcept { return value_ > o.value_; }
  bool operator>=(const Generation& o) const noexcept { return value_ >= o.value_; }

  static constexpr std::uint64_t max() noexcept { return UINT64_MAX - 1; }
  static constexpr std::uint64_t minimum_valid() noexcept { return 1; }

private:
  std::uint64_t value_ = 0;
};

// Identity tags -------------------------------------------------------------
struct HostIdTag {};
struct HostGenTag {};
struct DeviceIdTag {};
struct DeviceGenTag {};
struct DeviceBootIdTag {};
struct PortIdTag {};
struct SwitchIdTag {};
struct DecoderIdTag {};
struct WindowIdTag {};
struct RegionIdTag {};
struct RegionGenTag {};
struct PoolIdTag {};
struct PoolGenTag {};
struct CapacityLeaseIdTag {};
struct ReservationIdTag {};
struct ConsumerIdTag {};
struct ConsumerGenTag {};
struct WorkerIdTag {};
struct WorkerBootIdTag {};
struct CoordinatorEpochTag {};
struct EvidenceGenTag {};
struct PolicyGenTag {};
struct CapabilityIdTag {};

using HostId         = Id<HostIdTag>;
using HostGeneration = Generation<HostGenTag>;
using DeviceId       = Id<DeviceIdTag>;
using DeviceGeneration = Generation<DeviceGenTag>;
using DeviceBootId   = Id<DeviceBootIdTag>;
using PortId         = Id<PortIdTag>;
using SwitchId       = Id<SwitchIdTag>;
using DecoderId      = Id<DecoderIdTag>;
using WindowId       = Id<WindowIdTag>;
using RegionId       = Id<RegionIdTag>;
using RegionGeneration = Generation<RegionGenTag>;
using PoolId         = Id<PoolIdTag>;
using PoolGeneration = Generation<PoolGenTag>;
using CapacityLeaseId = Id<CapacityLeaseIdTag>;
using ReservationId  = Id<ReservationIdTag>;
using ConsumerId     = Id<ConsumerIdTag>;
using ConsumerGeneration = Generation<ConsumerGenTag>;
using WorkerId       = Id<WorkerIdTag>;
using WorkerBootId   = Id<WorkerBootIdTag>;
using CoordinatorEpoch = Generation<CoordinatorEpochTag>;
using EvidenceGeneration = Generation<EvidenceGenTag>;
using PolicyGeneration = Generation<PolicyGenTag>;

}  // namespace cxl_fabric

// std::hash support for typed ids and generations ----------------------------
namespace std {
template <class Tag>
struct hash<cxl_fabric::Id<Tag>> {
  size_t operator()(const cxl_fabric::Id<Tag>& v) const noexcept {
    return std::hash<std::string>{}(v.value());
  }
};
template <class Tag>
struct hash<cxl_fabric::Generation<Tag>> {
  size_t operator()(const cxl_fabric::Generation<Tag>& v) const noexcept {
    return std::hash<std::uint64_t>{}(v.value());
  }
};
}  // namespace std
