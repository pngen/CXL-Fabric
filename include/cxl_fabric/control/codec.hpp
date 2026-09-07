// ============================================================================
// CXL Fabric - control-plane payload codec
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/device.hpp"
#include "cxl_fabric/core/pool.hpp"
#include "cxl_fabric/core/region.hpp"
#include "cxl_fabric/protocol/protocol.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace cxl_fabric {
namespace codec {

inline void put_str(std::vector<std::uint8_t>& out, const std::string& s) {
  put_u32(out, std::uint32_t(s.size()));
  if (!s.empty()) put_bytes(out, reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}
inline bool get_str(const std::uint8_t* d, std::size_t len, std::size_t& pos, std::string& out) {
  std::uint32_t n; if (!get_u32(d, len, pos, n) || n > 65536u) return false;
  if (len < pos || pos > len - n) return false;
  out.assign(reinterpret_cast<const char*>(d + pos), n); pos += n; return true;
}

// PUBLISH_DEVICE payload layout:
// u64 gen | u64 bytes | u64 online | id(str) | host(str) | u32 vol | u32 state |
// u32 provenance | u32 locality | u32 accel | u8 evidence_current | u8 online
inline std::vector<std::uint8_t> encode_device(const CxlDevice& d) {
  std::vector<std::uint8_t> b;
  put_u64(b, d.generation.value());
  put_u64(b, d.total_physical_bytes);
  put_u64(b, d.online_bytes);
  put_str(b, d.id.str());
  put_str(b, d.host.str());
  put_u32(b, std::uint32_t(d.volatility));
  put_u32(b, std::uint32_t(d.state));
  put_u32(b, std::uint32_t(d.provenance));
  put_u32(b, std::uint32_t(d.economics.locality));
  put_u32(b, std::uint32_t(d.accelerator_access));
  put_u32(b, std::uint32_t(d.evidence_current ? 1 : 0));
  put_u32(b, std::uint32_t(d.state == DeviceState::ONLINE ? 1 : 0));
  return b;
}
inline bool decode_device(const std::vector<std::uint8_t>& p, CxlDevice& d) {
  std::size_t pos = 0;
  std::uint64_t g, bytes, online; std::string id, host;
  std::uint32_t vol, st, prov, loc, accel, evc, onl;
  if (!get_u64(p.data(), p.size(), pos, g) || !get_u64(p.data(), p.size(), pos, bytes) ||
      !get_u64(p.data(), p.size(), pos, online) || !get_str(p.data(), p.size(), pos, id) ||
      !get_str(p.data(), p.size(), pos, host) || !get_u32(p.data(), p.size(), pos, vol) ||
      !get_u32(p.data(), p.size(), pos, st) || !get_u32(p.data(), p.size(), pos, prov) ||
      !get_u32(p.data(), p.size(), pos, loc) || !get_u32(p.data(), p.size(), pos, accel) ||
      !get_u32(p.data(), p.size(), pos, evc) || !get_u32(p.data(), p.size(), pos, onl) || pos != p.size()) return false;
  d.id = DeviceId(id); d.host = HostId(host); d.generation = DeviceGeneration(g);
  d.total_physical_bytes = bytes; d.online_bytes = online;
  d.volatility = VolatilityKind(vol); d.state = DeviceState(st); d.provenance = Provenance(prov);
  d.economics.locality = LocalityClass(loc); d.accelerator_access = AcceleratorAccessState(accel);
  d.evidence_current = evc != 0;
  if (onl != 0) d.state = DeviceState::ONLINE;
  d.boot_id = DeviceBootId(id + "-boot");
  d.cxl_device_class = "Type-3";
  d.device_type = "memory";
  d.vendor_device_id = "VEND:0001 DEVI:0001";
  d.raw_identity = "control:" + id;
  d.health = HealthState::HEALTHY;
  d.lifecycle_note = "multiprocess";
  return true;
}

inline std::vector<std::uint8_t> encode_reserve(std::uint64_t bytes, const std::string& consumer) {
  std::vector<std::uint8_t> b; put_u64(b, bytes); put_str(b, consumer); return b;
}
inline bool decode_reserve(const std::vector<std::uint8_t>& p, std::uint64_t& bytes, std::string& consumer) {
  std::size_t pos = 0; return get_u64(p.data(), p.size(), pos, bytes) && get_str(p.data(), p.size(), pos, consumer) && pos == p.size();
}


inline void put_pool_member(std::vector<std::uint8_t>& b, const PoolMember& m) {
  put_str(b, m.device.str()); put_u64(b, m.device_generation.value()); put_u64(b, m.nominal_bytes);
  put_u32(b, m.online ? 1 : 0); put_u32(b, m.health_ok ? 1 : 0); put_str(b, m.failure_domain);
  put_u32(b, std::uint32_t(m.locality));
}
inline bool get_pool_member(const std::uint8_t* d, std::size_t len, std::size_t& pos, PoolMember& m) {
  std::string dev, fd; std::uint64_t gen, nominal; std::uint32_t onl, hok, loc;
  if (!get_str(d, len, pos, dev) || !get_u64(d, len, pos, gen) || !get_u64(d, len, pos, nominal) ||
      !get_u32(d, len, pos, onl) || !get_u32(d, len, pos, hok) || !get_str(d, len, pos, fd) ||
      !get_u32(d, len, pos, loc)) return false;
  m.device = DeviceId(dev); m.device_generation = DeviceGeneration(gen); m.nominal_bytes = nominal;
  m.online = onl != 0; m.health_ok = hok != 0; m.failure_domain = fd; m.locality = LocalityClass(loc);
  return true;
}
inline std::vector<std::uint8_t> encode_pool(const PoolId& pid, const PoolGeneration& gen,
                                             const std::vector<PoolMember>& members) {
  std::vector<std::uint8_t> b;
  put_str(b, pid.str()); put_u64(b, gen.value());
  put_u32(b, std::uint32_t(members.size()));
  for (const auto& m : members) put_pool_member(b, m);
  return b;
}
inline bool decode_pool(const std::vector<std::uint8_t>& p, PoolId& pid, PoolGeneration& gen,
                        std::vector<PoolMember>& members) {
  std::size_t pos = 0; std::string id; std::uint64_t g; std::uint32_t n;
  if (!get_str(p.data(), p.size(), pos, id) || !get_u64(p.data(), p.size(), pos, g) ||
      !get_u32(p.data(), p.size(), pos, n) || n > 100000u) return false;
  pid = PoolId(id); gen = PoolGeneration(g);
  for (std::uint32_t i = 0; i < n; ++i) { PoolMember m; if (!get_pool_member(p.data(), p.size(), pos, m)) return false; members.push_back(m); }
  return pos == p.size();
}
inline std::vector<std::uint8_t> encode_region(const RegionId& rid, const RegionGeneration& gen,
                                               const PoolId& pid, const std::vector<DeviceId>& members,
                                               std::uint64_t bytes, LocalityClass loc, TierClass tier,
                                               RegionState st) {
  std::vector<std::uint8_t> b;
  put_str(b, rid.str()); put_u64(b, gen.value()); put_str(b, pid.str());
  put_u32(b, std::uint32_t(members.size()));
  for (const auto& m : members) put_str(b, m.str());
  put_u64(b, bytes); put_u32(b, std::uint32_t(loc)); put_u32(b, std::uint32_t(tier)); put_u32(b, std::uint32_t(st));
  return b;
}
inline bool decode_region(const std::vector<std::uint8_t>& p, CxlRegion& r) {
  std::size_t pos = 0; std::string rid, pid; std::uint64_t g, bytes; std::uint32_t n, loc, tier, st;
  if (!get_str(p.data(), p.size(), pos, rid) || !get_u64(p.data(), p.size(), pos, g) ||
      !get_str(p.data(), p.size(), pos, pid) || !get_u32(p.data(), p.size(), pos, n) || n > 100000u) return false;
  r.id = RegionId(rid); r.generation = RegionGeneration(g); r.pool = PoolId(pid);
  for (std::uint32_t i = 0; i < n; ++i) { std::string m; if (!get_str(p.data(), p.size(), pos, m)) return false; r.member_devices.emplace_back(m); }
  if (!get_u64(p.data(), p.size(), pos, bytes) || !get_u32(p.data(), p.size(), pos, loc) ||
      !get_u32(p.data(), p.size(), pos, tier) || !get_u32(p.data(), p.size(), pos, st)) return false;
  r.region_capacity_bytes = bytes; r.locality = LocalityClass(loc); r.tier = TierClass(tier); r.state = RegionState(st);
  r.interleaved = r.member_devices.size() > 1; r.health = HealthState::HEALTHY;
  r.provenance = Provenance::SYNTHETIC; r.evidence_current = true;
  r.evidence_generation = EvidenceGeneration(1);
  return pos == p.size();
}

}  // namespace codec
}  // namespace cxl_fabric