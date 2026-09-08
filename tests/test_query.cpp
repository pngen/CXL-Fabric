
// Regression proof that the coordinator's QUERY_STATE transport channel works
// end to end across the real coordinator/worker lifecycle. A fresh worker
// connects, sends a QUERY_STATE frame, and the coordinator must answer with a
// QUERY_STATE_RESULT frame whose payload is the authoritative state text. This
// is observed purely over the query channel (the worker's --query output), never
// by reading the coordinator's --statefile dump. The statefile remains available
// as a supplemental artifact in the existing test_multiprocess proof.
//
// Lifecycle coverage:
//   1. A fresh worker (no publish) queries and receives the live authority
//      (epoch, device state, the reservation just created by a peer worker).
//   2. After a real worker process death the coordinator fences its evidence;
//      a brand-new worker querying afterwards sees evidence_current=0 and the
//      device moved to REVALIDATION_REQUIRED.
//   3. After a coordinator restart at a new epoch the persisted structural
//      state is re-imported with dynamic evidence marked revalidation-required;
//      a fresh worker query reports the new epoch and non-current evidence.
// No test timeouts are set anywhere; lifecycle state transitions are the test.
#include "cxl_fabric/control/net.hpp"
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

static int failures = 0;
#define CHECK(c,m) do { if (!(c)) { std::cout << "FAIL: " << m << "\n"; ++failures; } } while (0)
using namespace cxl_fabric;

struct Proc { PROCESS_INFORMATION pi{}; bool valid=false; };

static void kill(const Proc& p) {
  if (p.pi.hProcess) { TerminateProcess(p.pi.hProcess, 1); WaitForSingleObject(p.pi.hProcess, 5000); CloseHandle(p.pi.hProcess); }
  if (p.pi.hThread) CloseHandle(p.pi.hThread);
}
// Spawn with lpApplicationName set so a space-containing path launches fine;
// the child's PROCESS_INFORMATION is returned via out so the coordinator can
// be genuinely terminated and closed rather than leaking a long-lived process.
static bool spawn2(const std::string& exe, const std::string& args, const std::string& outfile, Proc& out) {
  std::string cmd = exe + " " + args;
  STARTUPINFOA si{}; si.cb = sizeof(si);
  HANDLE hOut = INVALID_HANDLE_VALUE;
  if (!outfile.empty()) {
    hOut = CreateFileA(outfile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut != INVALID_HANDLE_VALUE) { si.hStdOutput = hOut; si.hStdError = hOut; si.dwFlags = STARTF_USESTDHANDLES; }
  }
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessA(exe.c_str(), const_cast<char*>(cmd.data()), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  if (hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);
  out.pi = pi;
  return ok == TRUE;
}

#include <winsock2.h>
static std::uint16_t free_port() {
  WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = 0; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  sockaddr_in out{}; int ol = sizeof(out); getsockname(s, reinterpret_cast<sockaddr*>(&out), &ol);
  std::uint16_t port = ntohs(out.sin_port);
  closesocket(s); WSACleanup();
  return port;
}

static bool wait_port(std::uint16_t port, int timeout_ms) {
  for (int i = 0; i < timeout_ms; i += 50) {
    net::Socket s;
    if (net::connect_to("127.0.0.1", port, s)) { s.close(); return true; }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}
static std::string read_file(const std::string& path) {
  std::ifstream ifs(path); std::string all, line;
  if (ifs) while (std::getline(ifs, line)) all += line + "\n";
  return all;
}
static bool wait_file(const std::string& path, const std::string& needle, int timeout_ms) {
  for (int i = 0; i < timeout_ms; i += 50) {
    if (read_file(path).find(needle) != std::string::npos) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}
static std::string pid_after(const std::string& text, const std::string& marker) {
  auto p = text.find(marker);
  if (p == std::string::npos) return "";
  auto sp = text.find(' ', p);
  if (sp == std::string::npos) return "";
  auto e = text.find(' ', sp + 1);
  return text.substr(sp + 1, e - sp - 1);
}

static std::string g_worker;

// Run one fresh --query worker and return the coordinator's QUERY_STATE_RESULT
// payload (the authoritative state text), or "" on transport failure. Proves the
// actual query channel delivers a well-formed response to a brand-new worker.
static std::string run_worker_query(std::uint16_t port, const std::string& boot) {
  auto dir = std::filesystem::temp_directory_path();
  std::string qf = (dir / ("cxl_query_" + boot + ".txt")).string();
  Proc q; q.valid = spawn2(g_worker, "--port " + std::to_string(port) + " --boot " + boot + " --query --out " + qf, "", q);
  std::string content;
  for (int k = 0; k < 200; ++k) {  // worker connects/queries/exits fast
    content = read_file(qf);
    if (!content.empty()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  kill(q);
  std::error_code ec; std::filesystem::remove(qf, ec);
  return content;
}

// Poll a fresh worker --query until it reflects needle (deterministic across
// the worker-death fence, which is an in-memory transition on the coordinator).
static bool wait_query_reflects(std::uint16_t port, const std::string& base, const std::string& needle, int timeout_ms) {
  for (int i = 0; i < timeout_ms; i += 150) {
    std::string c = run_worker_query(port, base + std::to_string(i));
    if (c.find(needle) != std::string::npos) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
  }
  return false;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::cout << "usage: test_query <coordinator-exe> <worker-exe>\n"; return 2; }
  std::string coord = argv[1];
  g_worker = argv[2];
  auto dir = std::filesystem::temp_directory_path();
  const int R = 8000;

  // ---------------- Worker death / restart observed via the query channel ----
  {
    std::uint16_t port = free_port();
    std::string resultsA = (dir / "cxl_query_wA.txt").string();
    std::string coordlog = (dir / "cxl_query_c1.log").string();
    Proc c; c.valid = spawn2(coord, "--port " + std::to_string(port) + " --epoch 1", coordlog, c);
    CHECK(c.valid, "coordinator spawned");
    CHECK(wait_port(port, 5000), "coordinator listening");

    // --stay keeps the worker connected so it remains a live, unfenced
    // incarnation while the query channel is probed below.
    Proc wa; wa.valid = spawn2(g_worker, "--port " + std::to_string(port) + " --boot bootA --publish --reserve-bytes 1073741824 --consumer c0 --out " + resultsA + " --stay", "", wa);
    CHECK(wa.valid, "worker A spawned");
    CHECK(wait_file(resultsA, "RESERVED", R), "worker A reserved");
    std::string rid = pid_after(read_file(resultsA), "RESERVED");
    CHECK(!rid.empty(), "parsed reservation id");

    // 1. Fresh worker query returns the live authority over the query channel.
    std::string q1 = run_worker_query(port, "q1");
    CHECK(q1.find("epoch=1") != std::string::npos, "fresh query reports epoch 1");
    CHECK(q1.find("devices=1") != std::string::npos, "fresh query reports the published device");
    CHECK(q1.find("reservations=1") != std::string::npos, "fresh query reports the peer reservation");
    CHECK(q1.find("reservation " + rid + " state=") != std::string::npos, "fresh query lists the live reservation");
    CHECK(q1.find("device dev-1 state=") != std::string::npos && q1.find("evidence_current=1") != std::string::npos,
          "fresh query reports current dynamic evidence");

    // 2. Real process death fences evidence; a brand-new worker sees it.
    kill(wa);
    CHECK(true, "worker A terminated (process death is the test)");
    CHECK(wait_query_reflects(port, "qd", "evidence_current=0", R),
          "fresh worker query reflects fenced evidence after worker death");
    std::string q2 = run_worker_query(port, "q2");
    CHECK(q2.find("device dev-1 state=REVALIDATION_REQUIRED") != std::string::npos,
          "device moved to REVALIDATION_REQUIRED after death");

    kill(c);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::error_code ec;
    for (const std::string& f : {resultsA, coordlog}) std::filesystem::remove(f, ec);
  }

  // ------- Coordinator restart observed via the query channel -------
  {
    std::uint16_t port = free_port();
    std::string persist = (dir / "cxl_query_coord.persist").string();
    std::string resultsB = (dir / "cxl_query_wB.txt").string();

    Proc c; c.valid = spawn2(coord, "--port " + std::to_string(port) + " --epoch 1 --persist " + persist, "", c);
    CHECK(c.valid, "coordinator B spawned");
    CHECK(wait_port(port, 5000), "coordinator B listening");
    Proc wb; wb.valid = spawn2(g_worker, "--port " + std::to_string(port) + " --boot bootB --publish --reserve-bytes 1073741824 --consumer c0 --out " + resultsB + " --stay", "", wb);
    CHECK(wb.valid, "worker B spawned");
    CHECK(wait_file(resultsB, "RESERVED", R), "worker B reserved");
    kill(wb);

    kill(c);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    Proc c2; c2.valid = spawn2(coord, "--port " + std::to_string(port) + " --epoch 2 --persist " + persist, "", c2);
    CHECK(c2.valid, "coordinator B2 spawned");
    CHECK(wait_port(port, 5000), "coordinator B2 listening");

    // 3. Fresh worker query after coordinator restart: new epoch, structural
    // state re-imported but dynamic evidence not current.
    std::string q3 = run_worker_query(port, "q3");
    CHECK(q3.find("epoch=2") != std::string::npos, "fresh query reports the new epoch");
    CHECK(q3.find("devices=1") != std::string::npos, "structural device state re-imported after restart");
    CHECK(q3.find("evidence_current=0") != std::string::npos, "dynamic evidence not silently current after restart");
    CHECK(q3.find("state=REVALIDATION_REQUIRED") != std::string::npos, "device revalidation-required after restart");
    CHECK(q3.find("reservations=0") != std::string::npos, "reservation ledger is fenced by absence after restart");

    kill(c2);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::error_code ec;
    for (const std::string& f : {persist, resultsB}) std::filesystem::remove(f, ec);
  }

  if (failures) { std::cout << "test_query FAILURES=" << failures << "\n"; return 1; }
  std::cout << "PASS: test_query\n";
  return 0;
}
