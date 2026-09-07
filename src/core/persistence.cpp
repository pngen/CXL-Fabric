
// ============================================================================
// CXL Fabric - persistence implementation
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#include "cxl_fabric/persistence/persistence.hpp"
#include "cxl_fabric/core/enum_strings.hpp"
#include "cxl_fabric/protocol/protocol.hpp"
#include <fstream>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <cstdio>
#endif

namespace cxl_fabric {

namespace {

// Masked byte extraction avoids C4310 on intentional constant truncation.
inline std::uint8_t b8(std::uint32_t v) { return std::uint8_t(v & 0xFFu); }

// Bounds-checked binary reader over an in-memory payload.
struct Reader {
  const std::uint8_t* data;
  std::size_t len;
  std::size_t pos = 0;
  // returns false at end/overflow
  bool u8(std::uint8_t& v) { if (len < pos || pos > len - 1) return false; v = data[pos++]; return true; }
  bool u32(std::uint32_t& v) { return get_u32(data, len, pos, v); }
  bool u64(std::uint64_t& v) { return get_u64(data, len, pos, v); }
  bool bytes(std::uint8_t* out, std::size_t n) { return get_bytes(data, len, pos, out, n); }
  bool str(std::string& out) {
    std::uint32_t n;
    if (!u32(n)) return false;
    if (n > 65536u || len < pos || pos > len - n) return false;
    out.assign(reinterpret_cast<const char*>(data + pos), n);
    pos += n;
    return true;
  }
  bool vec_str(std::vector<std::string>& out) {
    std::uint32_t n;
    if (!u32(n) || n > 4096u) return false;
    for (std::uint32_t i = 0; i < n; ++i) { std::string s; if (!str(s)) return false; out.push_back(std::move(s)); }
    return true;
  }
};

void w_str(std::vector<std::uint8_t>& b, const std::string& s) {
  put_u32(b, std::uint32_t(s.size()));
  put_bytes(b, reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}
void w_vec_str(std::vector<std::uint8_t>& b, const std::vector<std::string>& v) {
  put_u32(b, std::uint32_t(v.size()));
  for (const auto& s : v) w_str(b, s);
}

}  // namespace

bool serialize_state(const PersistedState& state, std::vector<std::uint8_t>& out, std::string& err) {
  out.clear();
  if (state.devices.size() > 100000u || state.regions.size() > 100000u || state.pools.size() > 100000u) {
    err = "state exceeds persistence bounds"; return false;
  }
  put_u64(out, state.policy_generation.value());
  put_u32(out, std::uint32_t(state.devices.size()));
  for (const auto& kv : state.devices) {
    const CxlDevice& d = kv.second;
    w_str(out, d.id.str());
    put_u64(out, d.generation.value());
    w_str(out, d.boot_id.str());
    w_str(out, d.raw_identity);
    w_str(out, d.vendor_device_id);
    w_str(out, d.cxl_device_class);
    w_str(out, d.device_type);
    w_str(out, d.host.str());
    w_vec_str(out, []{ std::vector<std::string> v; return v; }());  // ports as strings
    w_vec_str(out, std::vector<std::string>{});
    put_u64(out, d.total_physical_bytes);
    put_u64(out, d.online_bytes);
    put_u32(out, std::uint32_t(d.volatility));
    put_u32(out, std::uint32_t(d.sharing));
    put_u32(out, std::uint32_t(d.multi_host_capable ? 1 : 0));
    put_u32(out, std::uint32_t(d.interleave_capable ? 1 : 0));
    put_u32(out, std::uint32_t(d.accelerator_access));
    put_u32(out, std::uint32_t(d.provenance));
    put_u64(out, d.evidence_generation.value());
    w_str(out, d.firmware_metadata);
    w_str(out, d.lifecycle_note);
    // capabilities
    put_u32(out, std::uint32_t(d.capabilities.size()));
    for (const auto& ckv : d.capabilities.all()) {
      const Capability& cap = ckv.second;
      put_u32(out, std::uint32_t(cap.klass));
      put_u32(out, std::uint32_t(cap.state));
      put_u64(out, cap.version);
      put_u32(out, std::uint32_t(cap.provenance));
      put_u64(out, cap.evidence_generation.value());
      put_u32(out, std::uint32_t(cap.evidence_current ? 1 : 0));
      w_str(out, cap.constraints);
    }
    // economics
    put_u32(out, std::uint32_t(d.economics.locality));
    put_u32(out, std::uint32_t(d.economics.tier));
    put_u32(out, std::uint32_t(d.economics.evidence_class));
    put_u32(out, std::uint32_t(d.economics.provenance));
    put_u64(out, d.economics.estimated_latency_ns);
    put_u64(out, d.economics.estimated_bandwidth_mbps);
    put_u32(out, d.economics.hop_count);
    put_u64(out, d.economics.migration_cost_bytes);
    put_u32(out, std::uint32_t(d.economics.failsafe_valid ? 1 : 0));
    w_str(out, d.economics.notes);
  }
  put_u32(out, std::uint32_t(state.regions.size()));
  for (const auto& kv : state.regions) {
    const CxlRegion& r = kv.second;
    w_str(out, r.id.str());
    put_u64(out, r.generation.value());
    w_str(out, r.pool.str());
    put_u32(out, std::uint32_t(r.member_devices.size()));
    for (const auto& m : r.member_devices) w_str(out, m.str());
    w_str(out, r.interleave_set);
    put_u64(out, r.region_capacity_bytes);
    put_u32(out, std::uint32_t(r.interleaved ? 1 : 0));
    put_u32(out, std::uint32_t(r.provenance));
    put_u64(out, r.evidence_generation.value());
    put_u32(out, std::uint32_t(r.device_local ? 1 : 0));
  }
  put_u32(out, std::uint32_t(state.pools.size()));
  for (const auto& kv : state.pools) {
    const CxlPool& p = kv.second;
    w_str(out, p.id.str());
    put_u64(out, p.generation.value());
    put_u32(out, std::uint32_t(p.members.size()));
    for (const auto& m : p.members) {
      w_str(out, m.device.str());
      put_u64(out, m.device_generation.value());
      put_u64(out, m.nominal_bytes);
      put_u32(out, std::uint32_t(m.online ? 1 : 0));
      put_u32(out, std::uint32_t(m.health_ok ? 1 : 0));
      w_str(out, m.failure_domain);
      put_u32(out, std::uint32_t(m.locality));
    }
    put_u64(out, p.total_governed_bytes);
    put_u64(out, p.committed_bytes);
    put_u64(out, p.reserved_bytes);
    put_u64(out, p.draining_bytes);
    put_u64(out, p.unavailable_bytes);
    w_str(out, p.admission_policy);
    w_vec_str(out, p.failure_domains);
    put_u32(out, std::uint32_t(p.locality_distribution));
    put_u32(out, std::uint32_t(p.health_summary));
    put_u32(out, std::uint32_t(p.provenance));
    put_u64(out, p.evidence_generation.value());
    put_u32(out, std::uint32_t(p.evidence_current ? 1 : 0));
    w_str(out, p.lifecycle_note);
  }
  // Integrity footer: CRC32 over the entire payload. Corruption, truncation,
  // and trailing garbage are all rejected by deserialize_state.
  std::uint32_t crc = crc32(out.data(), out.size());
  put_u32(out, crc);
  return true;
}

bool deserialize_state(const std::vector<std::uint8_t>& payload, PersistedState& state, std::string& err) {
  if (payload.size() < 4) { err = "payload too small"; return false; }
  std::size_t body_len = payload.size() - 4;
  std::uint32_t stored_crc = (std::uint32_t(payload[payload.size()-4]) << 24) |
                             (std::uint32_t(payload[payload.size()-3]) << 16) |
                             (std::uint32_t(payload[payload.size()-2]) << 8) |
                              std::uint32_t(payload[payload.size()-1]);
  if (crc32(payload.data(), body_len) != stored_crc) { err = "payload checksum mismatch"; return false; }
  Reader r{payload.data(), body_len};
  state = PersistedState{};
  std::uint64_t polgen;
  if (!r.u64(polgen)) { err = "missing policy generation"; return false; }
  state.policy_generation = PolicyGeneration(polgen);
  std::uint64_t eg = 0;
  std::uint32_t u = 0, ui = 0, dcount = 0;
  if (!r.u32(dcount) || dcount > 100000u) { err = "device count invalid"; return false; }
  for (std::uint32_t i = 0; i < dcount; ++i) {
    CxlDevice d;
    std::string sid;
    if (!r.str(sid)) { err = "device id"; return false; }
    d.id = DeviceId(sid);
    std::uint64_t g; if (!r.u64(g)) { err = "device gen"; return false; }
    d.generation = DeviceGeneration(g);
    std::string b; if (!r.str(b)) { err = "device boot"; return false; }
    d.boot_id = DeviceBootId(b);
    if (!r.str(d.raw_identity) || !r.str(d.vendor_device_id) || !r.str(d.cxl_device_class) ||
        !r.str(d.device_type)) { err = "device strings"; return false; }
    std::string hoststr; if (!r.str(hoststr)) { err = "device host"; return false; }
    d.host = HostId(hoststr);
    std::vector<std::string> tmp; if (!r.vec_str(tmp)) { err = "device ports"; return false; }
    std::vector<std::string> tmp2; if (!r.vec_str(tmp2)) { err = "device switch"; return false; }
    std::uint64_t tp; if (!r.u64(tp)) { err = "total"; return false; }
    d.total_physical_bytes = tp;
    if (!r.u64(d.online_bytes)) { err = "online"; return false; }
    if (!r.u32(u)) { err = "vol"; return false; }
    d.volatility = VolatilityKind(u);
    if (!r.u32(u)) { err = "sharing"; return false; }
    d.sharing = SharingMode(u);
    if (!r.u32(ui)) { err = "multi"; return false; }
    d.multi_host_capable = ui != 0;
    if (!r.u32(ui)) { err = "interleave"; return false; }
    d.interleave_capable = ui != 0;
    if (!r.u32(u)) { err = "accel"; return false; }
    d.accelerator_access = AcceleratorAccessState(u);
    if (!r.u32(u)) { err = "prov"; return false; }
    d.provenance = Provenance(u);
    if (!r.u64(eg)) { err = "evidence gen"; return false; }
    d.evidence_generation = EvidenceGeneration(eg);
    if (!r.str(d.firmware_metadata) || !r.str(d.lifecycle_note)) { err = "device notes"; return false; }
    std::uint32_t ccap; if (!r.u32(ccap) || ccap > 256u) { err = "cap count"; return false; }
    for (std::uint32_t ci = 0; ci < ccap; ++ci) {
      Capability cap;
      std::uint32_t k; if (!r.u32(k)) { err = "cap klass"; return false; }
      cap.klass = CapabilityClass(k);
      if (!r.u32(u)) { err = "cap state"; return false; }
      cap.state = SupportState(u);
      if (!r.u64(cap.version)) { err = "cap version"; return false; }
      if (!r.u32(u)) { err = "cap prov"; return false; }
      cap.provenance = Provenance(u);
      if (!r.u64(eg)) { err = "cap ev gen"; return false; }
      cap.evidence_generation = EvidenceGeneration(eg);
      if (!r.u32(ui)) { err = "cap current"; return false; }
      cap.evidence_current = ui != 0;
      if (!r.str(cap.constraints)) { err = "cap constraints"; return false; }
      d.capabilities.set(cap);
    }
    std::uint32_t el, et, ee, ep;
    if (!r.u32(el) || !r.u32(et) || !r.u32(ee) || !r.u32(ep)) { err = "econ enum"; return false; }
    d.economics.locality = LocalityClass(el);
    d.economics.tier = TierClass(et);
    d.economics.evidence_class = EvidenceClass(ee);
    d.economics.provenance = Provenance(ep);
    if (!r.u64(d.economics.estimated_latency_ns) || !r.u64(d.economics.estimated_bandwidth_mbps)) { err = "econ"; return false; }
    if (!r.u32(d.economics.hop_count) || !r.u64(d.economics.migration_cost_bytes)) { err = "econ2"; return false; }
    if (!r.u32(ui)) { err = "econ failsafe"; return false; }
    d.economics.failsafe_valid = ui != 0;
    if (!r.str(d.economics.notes)) { err = "econ notes"; return false; }
    state.devices[d.id] = std::move(d);
  }
  std::uint32_t rcount;
  if (!r.u32(rcount) || rcount > 100000u) { err = "region count"; return false; }
  for (std::uint32_t i = 0; i < rcount; ++i) {
    CxlRegion reg;
    std::string sid;
    if (!r.str(sid)) { err = "region id"; return false; }
    reg.id = RegionId(sid);
    std::uint64_t g; if (!r.u64(g)) { err = "region gen"; return false; }
    reg.generation = RegionGeneration(g);
    std::string poolstr; if (!r.str(poolstr)) { err = "region pool"; return false; }
    reg.pool = PoolId(poolstr);
    std::uint32_t mc; if (!r.u32(mc) || mc > 100000u) { err = "region members"; return false; }
    for (std::uint32_t mi = 0; mi < mc; ++mi) {
      std::string ms; if (!r.str(ms)) { err = "region member"; return false; }
      reg.member_devices.emplace_back(ms);
    }
    if (!r.str(reg.interleave_set)) { err = "region interleave"; return false; }
    if (!r.u64(reg.region_capacity_bytes)) { err = "region cap"; return false; }
    if (!r.u32(ui)) { err = "region interleaved"; return false; }
    reg.interleaved = ui != 0;
    if (!r.u32(u)) { err = "region prov"; return false; }
    reg.provenance = Provenance(u);
    if (!r.u64(eg)) { err = "region ev gen"; return false; }
    reg.evidence_generation = EvidenceGeneration(eg);
    if (!r.u32(ui)) { err = "region device_local"; return false; }
    reg.device_local = ui != 0;
    state.regions[reg.id] = std::move(reg);
  }
  std::uint32_t pcount;
  if (!r.u32(pcount) || pcount > 100000u) { err = "pool count"; return false; }
  for (std::uint32_t i = 0; i < pcount; ++i) {
    CxlPool p;
    std::string sid;
    if (!r.str(sid)) { err = "pool id"; return false; }
    p.id = PoolId(sid);
    std::uint64_t g; if (!r.u64(g)) { err = "pool gen"; return false; }
    p.generation = PoolGeneration(g);
    std::uint32_t mc; if (!r.u32(mc) || mc > 100000u) { err = "pool members"; return false; }
    for (std::uint32_t mi = 0; mi < mc; ++mi) {
      PoolMember m;
      std::string ds; if (!r.str(ds)) { err = "pool member device"; return false; }
      m.device = DeviceId(ds);
      std::uint64_t dg; if (!r.u64(dg)) { err = "pool member gen"; return false; }
      m.device_generation = DeviceGeneration(dg);
      if (!r.u64(m.nominal_bytes)) { err = "pool member nominal"; return false; }
      if (!r.u32(ui)) { err = "pool member online"; return false; }
      m.online = ui != 0;
      if (!r.u32(ui)) { err = "pool member health"; return false; }
      m.health_ok = ui != 0;
      if (!r.str(m.failure_domain)) { err = "pool member fd"; return false; }
      if (!r.u32(u)) { err = "pool member locality"; return false; }
      m.locality = LocalityClass(u);
      p.members.push_back(std::move(m));
    }
    if (!r.u64(p.total_governed_bytes) || !r.u64(p.committed_bytes) || !r.u64(p.reserved_bytes) ||
        !r.u64(p.draining_bytes) || !r.u64(p.unavailable_bytes)) { err = "pool accounting"; return false; }
    if (!r.str(p.admission_policy)) { err = "pool policy"; return false; }
    if (!r.vec_str(p.failure_domains)) { err = "pool domains"; return false; }
    if (!r.u32(u)) { err = "pool locality dist"; return false; }
    p.locality_distribution = LocalityClass(u);
    if (!r.u32(u)) { err = "pool health"; return false; }
    p.health_summary = HealthState(u);
    if (!r.u32(u)) { err = "pool prov"; return false; }
    p.provenance = Provenance(u);
    if (!r.u64(eg)) { err = "pool ev gen"; return false; }
    p.evidence_generation = EvidenceGeneration(eg);
    if (!r.u32(ui)) { err = "pool evidence"; return false; }
    p.evidence_current = ui != 0;
    if (!r.str(p.lifecycle_note)) { err = "pool note"; return false; }
    state.pools[p.id] = std::move(p);
  }
  if (r.pos != body_len) { err = "trailing garbage"; return false; }
  return true;
}

bool write_persisted(const std::filesystem::path& path, const std::vector<std::uint8_t>& payload, std::string& err) {
  if (payload.size() > kMaxPersistPayload) { err = "payload too large"; return false; }
  std::ofstream ofs(path.string() + ".tmp", std::ios::binary | std::ios::trunc);
  if (!ofs) { err = "cannot open temp"; return false; }
  std::uint8_t hdr[kPersistEnvelopeSize];
  std::uint32_t len32 = std::uint32_t(payload.size());
  hdr[0] = b8(kPersistMagic >> 24); hdr[1] = b8(kPersistMagic >> 16);
  hdr[2] = b8(kPersistMagic >> 8);  hdr[3] = b8(kPersistMagic);
  hdr[4] = b8(kPersistVersion >> 24); hdr[5] = b8(kPersistVersion >> 16);
  hdr[6] = b8(kPersistVersion >> 8);  hdr[7] = b8(kPersistVersion);
  hdr[8] = b8(len32 >> 24); hdr[9] = b8(len32 >> 16);
  hdr[10] = b8(len32 >> 8); hdr[11] = b8(len32);
  hdr[12] = 0; hdr[13] = 0; hdr[14] = 0; hdr[15] = 0;  // len high 32 bits (bound keeps this zero)
  std::vector<std::uint8_t> body;
  body.reserve(kPersistEnvelopeSize + payload.size());
  body.insert(body.end(), hdr, hdr + 16);
  body.insert(body.end(), payload.begin(), payload.end());
  std::uint32_t crc = crc32(body.data(), body.size());
  hdr[16] = b8(crc >> 24); hdr[17] = b8(crc >> 16);
  hdr[18] = b8(crc >> 8);  hdr[19] = b8(crc);
  ofs.write(reinterpret_cast<const char*>(hdr), kPersistEnvelopeSize);
  if (payload.size() > 0) ofs.write(reinterpret_cast<const char*>(payload.data()), std::streamsize(payload.size()));
  if (!ofs.flush()) { err = "flush failed"; ofs.close(); return false; }
  ofs.close();
#ifdef _WIN32
  std::wstring tmpw = path.wstring() + L".tmp";
  if (!MoveFileExW(tmpw.c_str(), path.wstring().c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    err = "rename failed"; return false;
  }
#else
  std::filesystem::rename(path.string() + ".tmp", path);
#endif
  return true;
}

bool read_persisted(const std::filesystem::path& path, std::vector<std::uint8_t>& payload, std::string& err) {
  std::ifstream ifs(path.string(), std::ios::binary);
  if (!ifs) { err = "cannot open"; return false; }
  std::uint8_t hdr[kPersistEnvelopeSize];
  ifs.read(reinterpret_cast<char*>(hdr), kPersistEnvelopeSize);
  if (ifs.gcount() != std::streamsize(kPersistEnvelopeSize)) { err = "truncated header"; return false; }
  std::uint32_t magic = (std::uint32_t(hdr[0]) << 24) | (std::uint32_t(hdr[1]) << 16) | (std::uint32_t(hdr[2]) << 8) | hdr[3];
  std::uint32_t ver = (std::uint32_t(hdr[4]) << 24) | (std::uint32_t(hdr[5]) << 16) | (std::uint32_t(hdr[6]) << 8) | hdr[7];
  std::uint32_t len32 = (std::uint32_t(hdr[8]) << 24) | (std::uint32_t(hdr[9]) << 16) | (std::uint32_t(hdr[10]) << 8) | hdr[11];
  if (magic != kPersistMagic) { err = "bad magic"; return false; }
  if (ver != kPersistVersion) { err = "bad version"; return false; }
  if (len32 > kMaxPersistPayload) { err = "oversized payload"; return false; }
  payload.assign(len32, 0);
  if (len32 > 0) { ifs.read(reinterpret_cast<char*>(payload.data()), std::streamsize(len32)); if (ifs.gcount() != std::streamsize(len32)) { err = "truncated payload"; return false; } }
  // read one more byte to detect trailing garbage
  char extra;
  if (ifs.read(&extra, 1)) { err = "trailing garbage"; return false; }
  std::vector<std::uint8_t> body;
  body.reserve(kPersistEnvelopeSize + payload.size());
  body.insert(body.end(), hdr, hdr + 16);
  body.insert(body.end(), payload.begin(), payload.end());
  std::uint32_t crc = (std::uint32_t(hdr[16]) << 24) | (std::uint32_t(hdr[17]) << 16) | (std::uint32_t(hdr[18]) << 8) | hdr[19];
  if (crc32(body.data(), body.size()) != crc) { err = "checksum mismatch"; return false; }
  return true;
}

}  // namespace cxl_fabric