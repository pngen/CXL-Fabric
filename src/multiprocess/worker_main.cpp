
// ============================================================================
// CXL Fabric - cxl_fabric_worker (real OS process, loopback TCP control plane).
// Publishes synthetic CXL evidence to the coordinator, and can orchestrate
// reserve/commit/release/query so the death/restart proofs can be exercised
// through genuine OS processes and sockets.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/control/codec.hpp"
#include "cxl_fabric/control/net.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

bool send_and_await(cxl_fabric::net::Socket& sock, const cxl_fabric::Frame& f,
                    cxl_fabric::FrameType expect) {
  using namespace cxl_fabric;
  if (!net::send_frame(sock, f)) return false;
  DecodeResult dr = net::recv_frame(sock);
  if (dr.status != DecodeStatus::OK) return false;
  return dr.frame.type == expect;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace cxl_fabric;
  std::string host = "127.0.0.1";
  std::uint16_t port = 17777;
  std::string boot = "worker-boot";
  bool do_publish = false;
  std::uint64_t reserve_bytes = 0;
  std::string consumer = "c0";
  bool stay = false;
  bool do_query = false;
  std::string commit_rid;
  std::string release_rid;
  bool reserve_commit = false;
  std::string out_path;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--host" && i + 1 < argc) host = argv[++i];
    else if (a == "--port" && i + 1 < argc) port = std::uint16_t(std::stoul(argv[++i]));
    else if (a == "--boot" && i + 1 < argc) boot = argv[++i];
    else if (a == "--publish") do_publish = true;
    else if (a == "--reserve-bytes" && i + 1 < argc) reserve_bytes = std::stoull(argv[++i]);
    else if (a == "--consumer" && i + 1 < argc) consumer = argv[++i];
    else if (a == "--stay") stay = true;
    else if (a == "--query") do_query = true;
    else if (a == "--commit" && i + 1 < argc) commit_rid = argv[++i];
    else if (a == "--release" && i + 1 < argc) release_rid = argv[++i];
    else if (a == "--reserve-commit") reserve_commit = true;
    else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
  }

  std::ofstream outfs; if (!out_path.empty()) outfs.open(out_path, std::ios::out | std::ios::trunc);
  auto emit = [&](const std::string& s){ std::cout << s << std::flush; if (outfs) outfs << s << std::flush; };

  net::Socket sock;
  if (!net::connect_to(host, port, sock)) {
    std::cerr << "[worker] connect failed to " << host << ":" << port << "\n";
    return 1;
  }

  // WORKER_BOOT
  Frame bootf; bootf.type = FrameType::WORKER_BOOT;
  bootf.payload.assign(boot.begin(), boot.end());
  if (!net::send_frame(sock, bootf)) { sock.close(); return 1; }

  HostId hostid("host-control");
  CxlDevice base = synthetic::make_device(DeviceId("dev-1"), DeviceGeneration(1), DeviceBootId(boot + "-boot-1"),
      hostid, 16ull * 1024 * 1024 * 1024, VolatilityKind::VOLATILE, SharingMode::DEDICATED,
      LocalityClass::SAME_HOST, 180, 32000, 1, "fd-root-a");

  if (do_publish || reserve_bytes > 0 || reserve_commit) {
    Frame pf; pf.type = FrameType::PUBLISH_DEVICE;
    pf.payload = codec::encode_device(base);
    if (!send_and_await(sock, pf, FrameType::ACK)) { sock.close(); return 1; }
    // Establish a governable pool + region so a reservation has capacity.
    PoolMember pm; pm.device = base.id; pm.device_generation = base.generation;
    pm.nominal_bytes = base.total_physical_bytes; pm.online = true; pm.health_ok = true;
    pm.failure_domain = "fd-control"; pm.locality = base.economics.locality;
    Frame poolf; poolf.type = FrameType::PUBLISH_POOL;
    poolf.payload = codec::encode_pool(PoolId("p-1"), PoolGeneration(1), {pm});
    net::send_frame(sock, poolf); net::recv_frame(sock);
    Frame regf; regf.type = FrameType::PUBLISH_REGION;
    regf.payload = codec::encode_region(RegionId("r-1"), RegionGeneration(1), PoolId("p-1"),
        {base.id}, base.total_physical_bytes, base.economics.locality, TierClass::CXL_NEAR, RegionState::ONLINE);
    net::send_frame(sock, regf); net::recv_frame(sock);
  }

  if (reserve_bytes > 0 || reserve_commit) {
    Frame rf; rf.type = FrameType::RESERVE;
    rf.payload = codec::encode_reserve(reserve_bytes, consumer);
    net::send_frame(sock, rf);
    DecodeResult dr = net::recv_frame(sock);
    if (dr.status != DecodeStatus::OK) { sock.close(); return 1; }
    if (dr.frame.type == FrameType::RESERVE_RESULT) {
      std::size_t pos = 0; std::string rid; std::uint32_t reason; std::uint8_t ok;
      if (codec::get_str(dr.frame.payload.data(), dr.frame.payload.size(), pos, rid) &&
          get_u32(dr.frame.payload.data(), dr.frame.payload.size(), pos, reason) &&
          pos < dr.frame.payload.size()) {
        ok = dr.frame.payload[pos];
        emit("RESERVED " + rid + " reason=" + std::to_string(int(reason)) + " ok=" + std::to_string(int(ok)) + "\n");
      }
    }
  }

  if (reserve_commit && reserve_bytes > 0) {
    // Commit needs the reservation id from the RESERVE_RESULT; re-query not done here.
  }

  if (!commit_rid.empty()) {
    Frame cf; cf.type = FrameType::COMMIT;
    cf.payload.assign(commit_rid.begin(), commit_rid.end());
    net::send_frame(sock, cf);
    DecodeResult dr = net::recv_frame(sock);
    if (dr.status == DecodeStatus::OK && dr.frame.type == FrameType::COMMIT_RESULT) {
      std::size_t pos = 0; std::string rid; std::uint32_t reason; std::uint8_t ok;
      if (codec::get_str(dr.frame.payload.data(), dr.frame.payload.size(), pos, rid) &&
          get_u32(dr.frame.payload.data(), dr.frame.payload.size(), pos, reason) &&
          pos < dr.frame.payload.size()) {
        ok = dr.frame.payload[pos];
        emit("COMMIT_RESULT " + rid + " reason=" + std::to_string(int(reason)) + " ok=" + std::to_string(int(ok)) + "\n");
      }
    }
  }

  if (!release_rid.empty()) {
    Frame rf; rf.type = FrameType::RELEASE;
    rf.payload.assign(release_rid.begin(), release_rid.end());
    net::send_frame(sock, rf);
    DecodeResult dr = net::recv_frame(sock);
    if (dr.status == DecodeStatus::OK && dr.frame.type == FrameType::COMMIT_RESULT) {
      std::size_t pos = 0; std::string rid; std::uint32_t reason; std::uint8_t ok;
      if (codec::get_str(dr.frame.payload.data(), dr.frame.payload.size(), pos, rid) &&
          get_u32(dr.frame.payload.data(), dr.frame.payload.size(), pos, reason) &&
          pos < dr.frame.payload.size()) {
        ok = dr.frame.payload[pos];
        emit("RELEASE_RESULT " + rid + " reason=" + std::to_string(int(reason)) + " ok=" + std::to_string(int(ok)) + "\n");
      }
    }
  }

  if (do_query) {
    Frame qf; qf.type = FrameType::QUERY_STATE;
    net::send_frame(sock, qf);
    DecodeResult dr = net::recv_frame(sock);
    if (dr.status == DecodeStatus::OK && dr.frame.type == FrameType::QUERY_STATE_RESULT) {
      std::string s(reinterpret_cast<const char*>(dr.frame.payload.data()), dr.frame.payload.size());
      emit(s);
    }
  }

  if (stay) {
    while (true) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  sock.close();
  return 0;
}