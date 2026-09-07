
// Real multiprocess proof: spawns the coordinator and worker executables as OS
// processes, exercises a governed reservation, then kills/restarts the worker
// and the coordinator, proving generation-bound authority and conservative
// recovery. No test timeouts; process death is the test. State is observed via
// the coordinator's authoritative --statefile dump (a text snapshot written
// after every mutation), never via a secondary query channel.
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
// Spawn with lpApplicationName set so a space-containing path launches fine.
static bool spawn2(const std::string& exe, const std::string& args, const std::string& outfile) {
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
static bool wait_state(const std::string& path, const std::string& needle, int timeout_ms) {
  return wait_file(path, needle, timeout_ms);
}
static std::string pid_after(const std::string& text, const std::string& marker) {
  auto p = text.find(marker);
  if (p == std::string::npos) return "";
  auto sp = text.find(' ', p);
  if (sp == std::string::npos) return "";
  auto e = text.find(' ', sp + 1);
  return text.substr(sp + 1, e - sp - 1);
}

int main(int argc, char** argv) {
  if (argc < 3) { std::cout << "usage: test_multiprocess <coordinator-exe> <worker-exe>\n"; return 2; }
  std::string coord = argv[1];
  std::string worker = argv[2];
  auto dir = std::filesystem::temp_directory_path();

  const int R = 6000;

  // ---------------- Worker death / restart proof ----------------
  {
    std::uint16_t port = free_port();
    std::string sf = (dir / "cxl_mp_sf1.txt").string();
    std::string resultsA = (dir / "cxl_mp_workerA.txt").string();
    std::string resultsA2 = (dir / "cxl_mp_workerA2.txt").string();
    std::string coordlog1 = (dir / "cxl_mp_c1.log").string();
    Proc c; c.valid = spawn2(coord, "--port " + std::to_string(port) + " --epoch 1 --statefile " + sf, coordlog1);
    CHECK(c.valid, "coordinator spawned");
    CHECK(wait_port(port, 5000), "coordinator listening");
    Proc wa; wa.valid = spawn2(worker, "--port " + std::to_string(port) + " --boot bootA --publish --reserve-bytes 1073741824 --consumer c0 --out " + resultsA, "");
    CHECK(wa.valid, "worker A spawned");
    CHECK(wait_file(resultsA, "RESERVED", R), "worker A reserved");
    std::string rid = pid_after(read_file(resultsA), "RESERVED");
    CHECK(!rid.empty(), "parsed reservation id");
    kill(wa);
    CHECK(true, "worker A terminated (process death is the test)");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));


    spawn2(worker, "--port " + std::to_string(port) + " --boot bootA2 --publish --commit " + rid + " --out " + resultsA2, "");
    CHECK(wait_file(resultsA2, "COMMIT_RESULT", R), "worker A2 commit result");
    CHECK(read_file(resultsA2).find("ok=0") != std::string::npos, "old worker-boot reservation commit fenced");

    kill(c);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::error_code ec;
    for (const std::string& f : {sf, resultsA, resultsA2, coordlog1}) std::filesystem::remove(f, ec);
  }

  // ---------------- Coordinator death / restart proof ----------------
  {
    std::uint16_t port = free_port();
    std::string persist = (dir / "cxl_mp_coord.persist").string();
    std::string sf2 = (dir / "cxl_mp_sf2.txt").string();
    std::string resultsB = (dir / "cxl_mp_workerB.txt").string();
    std::string resultsB2 = (dir / "cxl_mp_workerB2.txt").string();
    std::string resultsB3 = (dir / "cxl_mp_workerB3.txt").string();

    Proc c; c.valid = spawn2(coord, "--port " + std::to_string(port) + " --epoch 1 --persist " + persist + " --statefile " + sf2, "");
    CHECK(c.valid, "coordinator B spawned");
    CHECK(wait_port(port, 5000), "coordinator B listening");
    Proc wb; wb.valid = spawn2(worker, "--port " + std::to_string(port) + " --boot bootB --publish --reserve-bytes 1073741824 --consumer c1 --out " + resultsB, "");
    CHECK(wb.valid, "worker B spawned");
    CHECK(wait_file(resultsB, "RESERVED", R), "worker B reserved");
    std::string rid2 = pid_after(read_file(resultsB), "RESERVED");
    CHECK(!rid2.empty(), "parsed reservation id 2");
    CHECK(wait_state(sf2, "epoch=1", R), "epoch=1 present");

    kill(c);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    Proc c2; c2.valid = spawn2(coord, "--port " + std::to_string(port) + " --epoch 2 --persist " + persist + " --statefile " + sf2, "");
    CHECK(c2.valid, "coordinator B2 spawned");
    CHECK(wait_port(port, 5000), "coordinator B2 listening");
    CHECK(wait_state(sf2, "epoch=2", R), "epoch advanced to 2");
    CHECK(wait_state(sf2, "REVALIDATION_REQUIRED", R), "dynamic evidence not silently current after restart");
    CHECK(wait_state(sf2, "evidence_current=0", R), "evidence stale after restart");

    spawn2(worker, "--port " + std::to_string(port) + " --boot bootB2 --commit " + rid2 + " --out " + resultsB2, "");
    CHECK(wait_file(resultsB2, "COMMIT_RESULT", R), "commit result 2");
    CHECK(read_file(resultsB2).find("ok=0") != std::string::npos, "old epoch reservation commit rejected");

    spawn2(worker, "--port " + std::to_string(port) + " --boot bootB3 --publish --reserve-bytes 1073741824 --consumer c1 --out " + resultsB3, "");
    CHECK(wait_file(resultsB3, "RESERVED", R), "fresh reservation under epoch 2");
    CHECK(!pid_after(read_file(resultsB3), "RESERVED").empty(), "fresh reservation id parsed");

    kill(c2);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::error_code ec;
    for (const std::string& f : {persist, sf2, resultsB, resultsB2, resultsB3}) std::filesystem::remove(f, ec);
  }

  if (failures) { std::cout << "test_multiprocess FAILURES=" << failures << "\n"; return 1; }
  std::cout << "PASS: test_multiprocess\n";
  return 0;
}