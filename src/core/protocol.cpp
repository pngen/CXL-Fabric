// ============================================================================
// CXL Fabric - protocol codec implementation
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#include "cxl_fabric/protocol/protocol.hpp"
#include <cstring>

namespace cxl_fabric {

namespace {
// Accept every defined FrameType. QUERY_STATE (22) and QUERY_STATE_RESULT (23)
// are the wire types for the coordinator query channel; a hardcoded upper bound
// that predates them silently rejected a legitimate request as BAD_TYPE, which
// the coordinator treated as a transport error and closed the connection --
// the fresh-worker --query read race. Tie the bound to the highest enumerated
// value so newly added frame types remain decodable.
bool valid_type(std::uint16_t t) {
  return t >= 1 && t <= static_cast<std::uint16_t>(FrameType::QUERY_STATE_RESULT);
}
std::uint32_t load_u32(const std::uint8_t* p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
         (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
std::uint16_t load_u16(const std::uint8_t* p) {
  return std::uint16_t((std::uint16_t(p[0]) << 8) | std::uint16_t(p[1]));
}
void store_u32(std::uint8_t* p, std::uint32_t v) {
  p[0] = std::uint8_t(v >> 24); p[1] = std::uint8_t(v >> 16);
  p[2] = std::uint8_t(v >> 8);  p[3] = std::uint8_t(v);
}
void store_u16(std::uint8_t* p, std::uint16_t v) {
  p[0] = std::uint8_t(v >> 8); p[1] = std::uint8_t(v);
}
}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
  static std::uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k)
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < len; ++i)
    crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

std::vector<std::uint8_t> encode_frame(const Frame& f, std::uint16_t version) {
  std::vector<std::uint8_t> out(kFrameHeaderSize, 0);
  store_u32(out.data(), kProtocolMagic);
  store_u16(out.data() + 4, version);
  store_u16(out.data() + 6, std::uint16_t(f.type));
  if (f.payload.size() > kMaxPayloadBytes) return out;  // caller checked; keep header but empty
  store_u32(out.data() + 8, std::uint32_t(f.payload.size()));
  // checksum over magic..length (first 12 bytes) plus payload
  std::vector<std::uint8_t> body;
  body.reserve(kFrameHeaderSize + f.payload.size());
  body.insert(body.end(), out.data(), out.data() + 12);
  body.insert(body.end(), f.payload.begin(), f.payload.end());
  store_u32(out.data() + 12, crc32(body.data(), body.size()));
  out.insert(out.end(), f.payload.begin(), f.payload.end());
  return out;
}

DecodeResult decode_frame(const std::uint8_t* data, std::size_t len) {
  DecodeResult r;
  if (len < kFrameHeaderSize) { r.status = DecodeStatus::TRUNCATED; r.error = "header truncated"; return r; }
  std::uint32_t magic = load_u32(data);
  std::uint16_t version = load_u16(data + 4);
  std::uint16_t type = load_u16(data + 6);
  std::uint32_t length = load_u32(data + 8);
  std::uint32_t checksum = load_u32(data + 12);
  if (magic != kProtocolMagic) { r.status = DecodeStatus::BAD_MAGIC; r.error = "bad magic"; return r; }
  if (version != kProtocolVersion) { r.status = DecodeStatus::BAD_VERSION; r.error = "bad version"; return r; }
  if (!valid_type(type)) { r.status = DecodeStatus::BAD_TYPE; r.error = "bad type"; return r; }
  if (length > kMaxPayloadBytes) { r.status = DecodeStatus::OVERSIZED; r.error = "oversized payload"; return r; }
  std::size_t total = kFrameHeaderSize + std::size_t(length);
  if (total > len) { r.status = DecodeStatus::TRUNCATED; r.error = "payload truncated"; return r; }
  std::vector<std::uint8_t> body;
  body.reserve(total);
  body.insert(body.end(), data, data + 12);
  body.insert(body.end(), data + kFrameHeaderSize, data + total);
  if (crc32(body.data(), body.size()) != checksum) {
    r.status = DecodeStatus::BAD_CHECKSUM; r.error = "checksum mismatch"; return r;
  }
  r.frame.type = FrameType(type);
  r.frame.payload.assign(data + kFrameHeaderSize, data + total);
  r.consumed = total;
  if (total != len) { r.status = DecodeStatus::TRAILING_GARBAGE; r.error = "trailing bytes"; return r; }
  r.status = DecodeStatus::OK;
  return r;
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  std::uint8_t buf[4]; store_u32(buf, v); out.insert(out.end(), buf, buf + 4);
}
void put_u64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  std::uint8_t buf[8];
  buf[0] = std::uint8_t(v >> 56); buf[1] = std::uint8_t(v >> 48);
  buf[2] = std::uint8_t(v >> 40); buf[3] = std::uint8_t(v >> 32);
  buf[4] = std::uint8_t(v >> 24); buf[5] = std::uint8_t(v >> 16);
  buf[6] = std::uint8_t(v >> 8);  buf[7] = std::uint8_t(v);
  out.insert(out.end(), buf, buf + 8);
}
void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
  std::uint8_t buf[2]; store_u16(buf, v); out.insert(out.end(), buf, buf + 2);
}
void put_bytes(std::vector<std::uint8_t>& out, const std::uint8_t* data, std::size_t len) {
  out.insert(out.end(), data, data + len);
}

bool get_u32(const std::uint8_t* data, std::size_t len, std::size_t& pos, std::uint32_t& out) {
  if (len < pos || pos > len - 4) return false;
  out = load_u32(data + pos); pos += 4; return true;
}
bool get_u64(const std::uint8_t* data, std::size_t len, std::size_t& pos, std::uint64_t& out) {
  if (len < pos || pos > len - 8) return false;
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | std::uint64_t(data[pos + std::size_t(i)]);
  pos += 8; out = v; return true;
}
bool get_u16(const std::uint8_t* data, std::size_t len, std::size_t& pos, std::uint16_t& out) {
  if (len < pos || pos > len - 2) return false;
  out = load_u16(data + pos); pos += 2; return true;
}
bool get_bytes(const std::uint8_t* data, std::size_t len, std::size_t& pos, std::uint8_t* out, std::size_t n) {
  if (len < pos || pos > len - n) return false;
  std::memcpy(out, data + pos, n); pos += n; return true;
}

}  // namespace cxl_fabric
