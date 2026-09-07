
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include "cxl_fabric/persistence/persistence.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c,m) do { if (!(c)) { std::cout << "FAIL: " << m << "\n"; ++failures; } } while (0)
using namespace cxl_fabric;

int main() {
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  HostId host("host-persist");
  for (auto& d : synthetic::baseline_devices(host)) f.register_device(d, ctx);
  f.create_pool(synthetic::make_pool(PoolId("p-1"), PoolGeneration(1), {
      {DeviceId("dev-1"), DeviceGeneration(1), 16ull*1024*1024*1024, true,true,"fd-a",LocalityClass::SAME_HOST},
      {DeviceId("dev-2"), DeviceGeneration(1), 64ull*1024*1024*1024, true,true,"fd-b",LocalityClass::REMOTE_NUMA_DOMAIN} }), ctx);

  PersistedState st; f.export_persisted(st);
  std::vector<std::uint8_t> payload; std::string err;
  CHECK(serialize_state(st, payload, err), "serialize ok");

  // Round-trip through deserialize.
  PersistedState st2;
  CHECK(deserialize_state(payload, st2, err), "deserialize ok");
  CHECK(st2.devices.size() == 3 && st2.pools.size() == 1, "round-trip counts");

  // Corrupt one payload byte -> checksum mismatch -> reject.
  auto bad = payload; bad[bad.size()/2] ^= 0xFF;
  PersistedState st3;
  CHECK(!deserialize_state(bad, st3, err), "corrupt payload rejected");
  // Truncate -> reject.
  auto trunc = payload; trunc.resize(trunc.size()/2);
  CHECK(!deserialize_state(trunc, st3, err), "truncated payload rejected");
  // Trailing garbage -> reject (checksum footer mismatch).
  auto trail = payload; trail.push_back(0xAA);
  CHECK(!deserialize_state(trail, st3, err), "trailing garbage rejected");

  // Envelope write/read with atomic temp+rename.
  auto dir = std::filesystem::temp_directory_path();
  auto path = dir / "cxl_fabric_persist_test.bin";
  CHECK(write_persisted(path, payload, err), "write_persisted ok");
  std::vector<std::uint8_t> readback;
  CHECK(read_persisted(path, readback, err), "read_persisted ok");
  CHECK(readback == payload, "envelope round-trip exact");

  // Corrupt the file's magic -> read rejects.
  {
    std::ifstream ifs(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    std::vector<std::uint8_t> hacked = bytes; hacked[0] ^= 0xFF;
    auto hp = dir / "cxl_fabric_persist_test_hack.bin";
    std::ofstream ofs(hp, std::ios::binary); ofs.write((const char*)hacked.data(), hacked.size()); ofs.close();
    std::vector<std::uint8_t> out;
    CHECK(!read_persisted(hp, out, err), "bad magic rejected");
  }
  // Truncate the file -> read rejects.
  {
    std::ifstream ifs(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    bytes.resize(bytes.size()/2);
    auto tp = dir / "cxl_fabric_persist_test_trunc.bin";
    std::ofstream ofs(tp, std::ios::binary); ofs.write((const char*)bytes.data(), bytes.size()); ofs.close();
    std::vector<std::uint8_t> out;
    CHECK(!read_persisted(tp, out, err), "truncated file rejected");
  }
  // Trailing garbage append -> read rejects.
  {
    std::ifstream ifs(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    bytes.push_back(0x55);
    auto tp = dir / "cxl_fabric_persist_test_trail.bin";
    std::ofstream ofs(tp, std::ios::binary); ofs.write((const char*)bytes.data(), bytes.size()); ofs.close();
    std::vector<std::uint8_t> out;
    CHECK(!read_persisted(tp, out, err), "file trailing garbage rejected");
  }
  // Oversized payload header -> read rejects.
  {
    auto op = dir / "cxl_fabric_persist_test_oversized.bin";
    std::vector<std::uint8_t> hdr(20, 0);
    hdr[0]=0x43; hdr[1]=0x58; hdr[2]=0x4C; hdr[3]=0x50; hdr[4]=0; hdr[5]=0; hdr[6]=0; hdr[7]=1;
    hdr[8]=0x20; hdr[9]=0x00; hdr[10]=0x00; hdr[11]=0x00;  // len=0x20000000 > bound
    std::ofstream ofs(op, std::ios::binary); ofs.write((const char*)hdr.data(), hdr.size()); ofs.close();
    std::vector<std::uint8_t> out;
    CHECK(!read_persisted(op, out, err), "oversized payload rejected");
  }

  // Clean up temp files.
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(dir / "cxl_fabric_persist_test_hack.bin", ec);
  std::filesystem::remove(dir / "cxl_fabric_persist_test_trunc.bin", ec);
  std::filesystem::remove(dir / "cxl_fabric_persist_test_trail.bin", ec);
  std::filesystem::remove(dir / "cxl_fabric_persist_test_oversized.bin", ec);

  if (failures) { std::cout << "test_persistence FAILURES=" << failures << "\n"; return 1; }
  std::cout << "PASS: test_persistence\n";
  return 0;
}
