
// ============================================================================
// CXL Fabric - cxl_fabric_coordinator (real OS process, loopback TCP control
// plane). Maintains a thread-safe Fabric, tracks workers, fences stale evidence
// on worker death, and optionally persists/reloads durable structural state.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#include "cxl_fabric/control/codec.hpp"
#include "cxl_fabric/control/net.hpp"
#include "cxl_fabric/core/enum_strings.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include "cxl_fabric/persistence/persistence.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_running{true};
void signal_handler(int) { g_running = false; }

struct WorkerState {
  cxl_fabric::WorkerBootId boot;
  std::vector<cxl_fabric::DeviceId> devices;
  std::vector<cxl_fabric::ReservationId> reservations;
  bool dead = false;
};

class Coordinator {
public:
  Coordinator(std::uint16_t port, std::uint64_t epoch, std::string persist, std::string statefile)
    : port_(port), persist_(std::move(persist)), statefile_(std::move(statefile)),
      fabric_(cxl_fabric::CoordinatorEpoch(epoch), cxl_fabric::PolicyGeneration(1)) {
    if (!persist_.empty() && std::filesystem::exists(persist_)) {
      std::vector<std::uint8_t> payload;
      std::string err;
      if (cxl_fabric::read_persisted(persist_, payload, err)) {
        cxl_fabric::PersistedState st;
        if (cxl_fabric::deserialize_state(payload, st, err)) {
          fabric_.import_persisted(st);
          std::cout << "[coordinator] recovered durable state; dynamic evidence marked REVALIDATION_REQUIRED\n";
        } else std::cout << "[coordinator] persisted state rejected: " << err << "\n";
      } else std::cout << "[coordinator] persisted state rejected: " << err << "\n";
    }
    write_statefile();
  }

  bool start() {
    if (!cxl_fabric::net::listen_on(port_, listener_)) {
      std::cerr << "[coordinator] failed to listen on port " << port_ << "\n";
      return false;
    }
    accept_thread_ = std::thread([this]{ accept_loop(); });
    accept_thread_.detach();
    return true;
  }

  void run() { while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(100)); }

  void stop() {
    g_running = false;
    listener_.close();
    if (!persist_.empty()) persist_now();
  }

private:
  void accept_loop() {
    while (g_running) {
      cxl_fabric::net::Socket cs;
      if (!cxl_fabric::net::accept_socket(listener_, cs)) { if (!g_running) break; continue; }
      auto ws = std::make_shared<WorkerState>();
      std::thread([this, cs, ws]{ handle(cs, ws); }).detach();
    }
  }

  void handle(cxl_fabric::net::Socket client, std::shared_ptr<WorkerState> ws) {
    using namespace cxl_fabric;
    DecodeResult first = net::recv_frame(client);
    if (first.status != DecodeStatus::OK || first.frame.type != FrameType::WORKER_BOOT) { client.close(); return; }
    std::string bootid(reinterpret_cast<const char*>(first.frame.payload.data()), first.frame.payload.size());
    ws->boot = WorkerBootId(bootid);
    std::cout << "[coordinator] worker booted: " << bootid << "\n"; std::cout.flush();
    std::string sid_key = bootid;
    { std::lock_guard<std::mutex> lock(workers_mu_); workers_[sid_key] = ws; }
    while (g_running) {
      DecodeResult dr = net::recv_frame(client);
      if (dr.status != DecodeStatus::OK) { appl_worker_death(sid_key); break; }
      dispatch(client, ws, dr.frame);
    }
    client.close();
    { std::lock_guard<std::mutex> lock(workers_mu_); if (workers_.count(sid_key)) workers_.erase(sid_key); }
  }

  void dispatch(cxl_fabric::net::Socket client, std::shared_ptr<WorkerState> ws, const cxl_fabric::Frame& f) {
    using namespace cxl_fabric;
    AuthorityContext ctx;
    ctx.coordinator_epoch = fabric_.epoch();
    ctx.policy_generation = fabric_.policy_generation();
    ctx.worker_boot_id = ws->boot;
    ctx.consumer_generation = ConsumerGeneration(1);
    switch (f.type) {
      case FrameType::PUBLISH_DEVICE: {
        CxlDevice d;
        if (!codec::decode_device(f.payload, d)) { send_error(client, "decode_device"); break; }
        DecisionReason dr = fabric_.register_device(d, ctx);
        if (dr == DecisionReason::OK) ws->devices.push_back(d.id);
        send_ack(client, dr);
        break;
      }
      case FrameType::RESERVE: {
        std::uint64_t bytes; std::string consumer;
        if (!codec::decode_reserve(f.payload, bytes, consumer)) { send_error(client, "decode_reserve"); break; }
        AdmissionRequest req; req.bytes = bytes; req.consumer = ConsumerId(consumer);
        ReserveOutcome ro = fabric_.reserve(req, ctx);
        Frame resp; resp.type = FrameType::RESERVE_RESULT;
        codec::put_str(resp.payload, ro.reservation.id.str());
        put_u32(resp.payload, std::uint32_t(ro.reason));
        resp.payload.push_back(ro.ok ? 1 : 0);
        net::send_frame(client, resp);
        if (ro.ok) ws->reservations.push_back(ro.reservation.id);
        break;
      }
      case FrameType::COMMIT: {
        std::string rid(reinterpret_cast<const char*>(f.payload.data()), f.payload.size());
        ReserveOutcome ro = fabric_.commit(ReservationId(rid), ctx);
        send_reserve_result(client, ro);
        break;
      }
      case FrameType::RELEASE: {
        std::string rid(reinterpret_cast<const char*>(f.payload.data()), f.payload.size());
        ReserveOutcome ro = fabric_.release(ReservationId(rid), ctx);
        send_reserve_result(client, ro);
        break;
      }
      case FrameType::QUERY_STATE: {
        std::string s = fabric_state_text();
        Frame resp; resp.type = FrameType::QUERY_STATE_RESULT;
        resp.payload.assign(s.begin(), s.end());
        net::send_frame(client, resp);
        break;
      }
      case FrameType::PUBLISH_POOL: {
        PoolId pid; PoolGeneration pgen; std::vector<PoolMember> members;
        if (!codec::decode_pool(f.payload, pid, pgen, members)) { send_error(client, "decode_pool"); break; }
        CxlPool pool; pool.id = pid; pool.generation = pgen; pool.members = members;
        pool.admission_policy = "default"; pool.state = PoolState::AVAILABLE;
        pool.health_summary = HealthState::HEALTHY; pool.provenance = Provenance::SYNTHETIC;
        pool.evidence_current = true; pool.evidence_generation = EvidenceGeneration(1);
        DecisionReason drp = fabric_.create_pool(pool, ctx);
        if (drp == DecisionReason::DUPLICATE_IDENTITY) drp = fabric_.revalidate_pool(pool.id, ctx);
        send_ack(client, drp);
        break;
      }
      case FrameType::PUBLISH_REGION: {
        CxlRegion reg;
        if (!codec::decode_region(f.payload, reg)) { send_error(client, "decode_region"); break; }
        DecisionReason drr = fabric_.create_region(reg, ctx);
        if (drr == DecisionReason::DUPLICATE_IDENTITY) drr = fabric_.revalidate_region(reg.id, ctx);
        send_ack(client, drr);
        break;
      }
      case FrameType::SHUTDOWN: { g_running = false; break; }
      default: send_error(client, "unsupported frame type"); break;
    }
    if (f.type == FrameType::PUBLISH_DEVICE || f.type == FrameType::RESERVE ||
        f.type == FrameType::COMMIT || f.type == FrameType::RELEASE ||
        f.type == FrameType::REVALIDATE) maybe_persist();
  }

  void send_ack(cxl_fabric::net::Socket client, cxl_fabric::DecisionReason r) {
    cxl_fabric::Frame resp; resp.type = cxl_fabric::FrameType::ACK;
    cxl_fabric::put_u32(resp.payload, std::uint32_t(r));
    cxl_fabric::net::send_frame(client, resp);
  }
  void send_error(cxl_fabric::net::Socket client, const std::string& msg) {
    cxl_fabric::Frame resp; resp.type = cxl_fabric::FrameType::ERROR_REPORT;
    resp.payload.assign(msg.begin(), msg.end());
    cxl_fabric::net::send_frame(client, resp);
  }
  void send_reserve_result(cxl_fabric::net::Socket client, const cxl_fabric::ReserveOutcome& ro) {
    cxl_fabric::Frame resp; resp.type = cxl_fabric::FrameType::COMMIT_RESULT;
    cxl_fabric::codec::put_str(resp.payload, ro.reservation.id.str());
    cxl_fabric::put_u32(resp.payload, std::uint32_t(ro.reason));
    resp.payload.push_back(ro.ok ? 1 : 0);
    cxl_fabric::net::send_frame(client, resp);
  }

  void appl_worker_death(const std::string& sid_key) {
    using namespace cxl_fabric;
    std::lock_guard<std::mutex> lock(workers_mu_);
    auto it = workers_.find(sid_key);
    if (it == workers_.end() || it->second->dead) return;
    it->second->dead = true;
    std::cout << "[coordinator] worker death detected: " << sid_key << " -> fencing evidence\n"; std::cout.flush();
    AuthorityContext ctx; ctx.coordinator_epoch = fabric_.epoch();
    ctx.policy_generation = fabric_.policy_generation();
    for (const auto& d : it->second->devices) fabric_.mark_device_revalidation(d, ctx);
    if (!statefile_.empty()) write_statefile();
  }

  std::string fabric_state_text() const {
    using namespace cxl_fabric;
    StateSnapshot snap = fabric_.snapshot();
    std::string s;
    s += "epoch=" + std::to_string(snap.coordinator_epoch.value()) + "\n";
    s += "devices=" + std::to_string(snap.devices.size()) + "\n";
    s += "reservations=" + std::to_string(snap.reservations.size()) + "\n";
    for (const auto& kv : snap.devices) {
      s += "device " + kv.first.str() + " state=" + std::string(to_string(kv.second.state)) +
           " evidence_current=" + (kv.second.evidence_current ? "1" : "0") + "\n";
    }
    for (const auto& kv : snap.reservations) {
      s += "reservation " + kv.first.str() + " state=" + std::string(to_string(kv.second.state)) + "\n";
    }
    return s;
  }

  void maybe_persist() { if (!persist_.empty()) persist_now(); if (!statefile_.empty()) write_statefile(); }

  void write_statefile() {
    if (statefile_.empty()) return;
    std::ofstream ofs(statefile_, std::ios::out | std::ios::trunc);
    if (ofs) ofs << fabric_state_text();
  }

  void persist_now() {
    using namespace cxl_fabric;
    PersistedState st;
    if (fabric_.export_persisted(st)) {
      std::vector<std::uint8_t> payload; std::string err;
      if (serialize_state(st, payload, err)) {
        if (write_persisted(persist_, payload, err)) std::cout << "[coordinator] persisted\n";
        else std::cout << "[coordinator] persist failed: " << err << "\n";
      } else std::cout << "[coordinator] serialize failed: " << err << "\n";
    }
  }

  std::uint16_t port_;
  std::string persist_;
  std::string statefile_;
  cxl_fabric::Fabric fabric_;
  cxl_fabric::net::Socket listener_;
  std::thread accept_thread_;
  std::mutex workers_mu_;
  std::map<std::string, std::shared_ptr<WorkerState>> workers_;
};

}  // namespace

int main(int argc, char** argv) {
  std::uint16_t port = 17777;
  std::uint64_t epoch = 1;
  std::string persist;
  std::string statefile;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) port = std::uint16_t(std::stoul(argv[++i]));
    else if (a == "--epoch" && i + 1 < argc) epoch = std::stoull(argv[++i]);
    else if (a == "--persist" && i + 1 < argc) persist = argv[++i];
    else if (a == "--statefile" && i + 1 < argc) statefile = argv[++i];
  }
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  Coordinator c(port, epoch, persist, statefile);
  if (!c.start()) return 2;
  std::cout.flush();
  c.run();
  c.stop();
  return 0;
}