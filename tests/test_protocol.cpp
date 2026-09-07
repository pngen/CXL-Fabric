
#include "cxl_fabric/protocol/protocol.hpp"
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c,m) do { if (!(c)) { std::cout << "FAIL: " << m << "\n"; ++failures; } } while (0)
using namespace cxl_fabric;

int main() {
  // Valid frame, zero-length payload.
  Frame f; f.type = FrameType::WORKER_BOOT;
  auto bytes = encode_frame(f);
  DecodeResult d = decode_frame(bytes.data(), bytes.size());
  CHECK(d.status == DecodeStatus::OK, "zero-length payload decode ok");
  CHECK(d.frame.type == FrameType::WORKER_BOOT, "frame type round-trip");
  CHECK(d.consumed == bytes.size(), "consumed exact");

  // Valid frame with payload.
  Frame f2; f2.type = FrameType::PUBLISH_DEVICE; f2.payload = {1,2,3,4,5};
  auto b2 = encode_frame(f2);
  DecodeResult d2 = decode_frame(b2.data(), b2.size());
  CHECK(d2.status == DecodeStatus::OK && d2.frame.payload == f2.payload, "payload round-trip");

  // Bad magic.
  auto bm = b2; bm[0] ^= 0xFF;
  CHECK(decode_frame(bm.data(), bm.size()).status == DecodeStatus::BAD_MAGIC, "bad magic rejected");
  // Bad version.
  auto bv = b2; bv[4] ^= 0xFF;
  CHECK(decode_frame(bv.data(), bv.size()).status == DecodeStatus::BAD_VERSION, "bad version rejected");
  // Bad type.
  auto bt = b2; bt[6] = 0xFF; bt[7] = 0xFF;
  CHECK(decode_frame(bt.data(), bt.size()).status == DecodeStatus::BAD_TYPE, "bad type rejected");
  // Oversized length.
  auto bo = b2; bo[8] = 0x7F; bo[9] = 0xFF; bo[10] = 0xFF; bo[11] = 0xFF;
  CHECK(decode_frame(bo.data(), bo.size()).status == DecodeStatus::OVERSIZED, "oversized rejected");
  // Truncated.
  std::vector<std::uint8_t> trunc(b2.begin(), b2.begin() + b2.size() - 3);
  CHECK(decode_frame(trunc.data(), trunc.size()).status == DecodeStatus::TRUNCATED, "truncated rejected");
  // Checksum mismatch.
  auto cs = b2; cs[cs.size()-1] ^= 0xFF;
  CHECK(decode_frame(cs.data(), cs.size()).status == DecodeStatus::BAD_CHECKSUM, "checksum mismatch rejected");
  // Trailing garbage.
  auto tg = b2; tg.push_back(0xAA);
  CHECK(decode_frame(tg.data(), tg.size()).status == DecodeStatus::TRAILING_GARBAGE, "trailing garbage rejected");

  // Byte packing helpers.
  std::vector<std::uint8_t> packed;
  put_u32(packed, 0x01020304);
  put_u16(packed, 0x0506);
  put_u64(packed, 0x0102030405060708ull);
  CHECK(packed.size() == 4 + 2 + 8, "packed length");
  std::size_t pos = 0; std::uint32_t u32; std::uint16_t u16; std::uint64_t u64;
  CHECK(get_u32(packed.data(), packed.size(), pos, u32) && u32 == 0x01020304, "get u32");
  CHECK(get_u16(packed.data(), packed.size(), pos, u16) && u16 == 0x0506, "get u16");
  CHECK(get_u64(packed.data(), packed.size(), pos, u64) && u64 == 0x0102030405060708ull, "get u64");
  // One-past-end reads rejected.
  pos = packed.size(); std::uint32_t dummy;
  CHECK(!get_u32(packed.data(), packed.size(), pos, dummy), "get u32 beyond end rejected");

  if (failures) { std::cout << "test_protocol FAILURES=" << failures << "\n"; return 1; }
  std::cout << "PASS: test_protocol\n";
  return 0;
}
