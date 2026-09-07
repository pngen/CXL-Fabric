// ============================================================================
// CXL Fabric - bounded, checksummed wire protocol for the multiprocess plane
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cxl_fabric {

// Wire constants. A frame is: magic(4) | version(2) | type(2) | length(4) |
// checksum(4) = 16-byte header followed by exactly `length` payload bytes.
inline constexpr std::uint32_t kProtocolMagic = 0x43584C46u;  // C X L F
inline constexpr std::uint16_t kProtocolVersion = 1u;
inline constexpr std::size_t   kFrameHeaderSize = 16u;
inline constexpr std::size_t   kMaxPayloadBytes = 65536u;

enum class FrameType : std::uint16_t {
  HELLO = 1, WORKER_BOOT = 2, WORKER_DEAD = 3, PUBLISH_DEVICE = 4,
  PUBLISH_CAPABILITY = 5, PUBLISH_REGION = 6, PUBLISH_POOL = 7,
  PUBLISH_HEALTH = 8, PUBLISH_LOCALITY = 9, RESERVE = 10, RESERVE_RESULT = 11,
  COMMIT = 12, COMMIT_RESULT = 13, RELEASE = 14, RELEASE_RESULT = 15,
  REVALIDATE = 16, REVALIDATE_RESULT = 17, EPOCH = 18, ERROR_REPORT = 19,
  SHUTDOWN = 20, ACK = 21, QUERY_STATE = 22, QUERY_STATE_RESULT = 23
};

struct Frame {
  FrameType type = FrameType::HELLO;
  std::vector<std::uint8_t> payload;
};

enum class DecodeStatus {
  OK, BAD_MAGIC, BAD_VERSION, BAD_TYPE, BAD_LENGTH, OVERSIZED, TRUNCATED,
  BAD_CHECKSUM, TRAILING_GARBAGE, MALFORMED
};

struct DecodeResult {
  DecodeStatus status = DecodeStatus::MALFORMED;
  Frame frame;
  std::size_t consumed = 0;
  std::string error;
};

std::vector<std::uint8_t> encode_frame(const Frame& f, std::uint16_t version = kProtocolVersion);
DecodeResult decode_frame(const std::uint8_t* data, std::size_t len);
std::uint32_t crc32(const std::uint8_t* data, std::size_t len);

// Payload byte packing helpers (big-endian).
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v);
void put_u64(std::vector<std::uint8_t>& out, std::uint64_t v);
void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v);
void put_bytes(std::vector<std::uint8_t>& out, const std::uint8_t* data, std::size_t len);
bool get_u32(const std::uint8_t* data, std::size_t len, std::size_t& pos, std::uint32_t& out);
bool get_u64(const std::uint8_t* data, std::size_t len, std::size_t& pos, std::uint64_t& out);
bool get_u16(const std::uint8_t* data, std::size_t len, std::size_t& pos, std::uint16_t& out);
bool get_bytes(const std::uint8_t* data, std::size_t len, std::size_t& pos, std::uint8_t* out, std::size_t n);

}  // namespace cxl_fabric