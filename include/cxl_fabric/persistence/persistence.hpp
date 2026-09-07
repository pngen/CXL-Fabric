// ============================================================================
// CXL Fabric - versioned, checksummed, atomic persistence
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/device.hpp"
#include "cxl_fabric/core/pool.hpp"
#include "cxl_fabric/core/region.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace cxl_fabric {

// The durable structural subset of fabric state. Dynamic evidence (online
// state, health, accelerator access visible to a boot-bound consumer) is never
// persisted as CURRENT; on load it must be re-observed.
struct PersistedState {
  PolicyGeneration policy_generation;
  std::map<DeviceId, CxlDevice> devices;
  std::map<RegionId, CxlRegion> regions;
  std::map<PoolId, CxlPool> pools;
};

inline constexpr std::uint32_t kPersistMagic = 0x43584C50u;   // C X L P
inline constexpr std::uint32_t kPersistVersion = 1u;
inline constexpr std::size_t   kPersistEnvelopeSize = 20u;    // magic(4)+ver(4)+len(8)+crc(4)
inline constexpr std::size_t   kMaxPersistPayload = 1u << 24; // 16 MiB bound

bool serialize_state(const PersistedState& state, std::vector<std::uint8_t>& out, std::string& err);
bool deserialize_state(const std::vector<std::uint8_t>& payload, PersistedState& state, std::string& err);
bool write_persisted(const std::filesystem::path& path, const std::vector<std::uint8_t>& payload, std::string& err);
bool read_persisted(const std::filesystem::path& path, std::vector<std::uint8_t>& payload, std::string& err);

}  // namespace cxl_fabric
