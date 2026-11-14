// Real OS-process proof: when a worker dies, the coordinator stops holding the
// capacity of its uncommitted reservations (no other incarnation can commit or
// release them). Observed over the QUERY_STATE channel.
#include "cxl_fabric/control/net.hpp"
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

static int failures = 0;
#define CHECK(c,m) do { if (!(c)) { std::cout << "FAIL: " << m << "\n"; ++failures; } } while (0)
using namespace cxl_fabric;

struct Proc { PROCESS_INFORMATION pi{}; bool valid = false; };

static void kill(const Proc& p) {
  if (p.pi.hProcess) { TerminateProcess(p.pi.hProcess, 1); WaitForSingleObject(p.pi.hProcess, 5000); CloseHandle(p.pi.hProcess); }
  if (p.pi.hThread) CloseHandle(p.pi.hThread);
}

static bool spawn2(const std::string& exe, const std::string& args, Proc& out) {
  std::string cmd = exe + " " + args;
  STARTUPINFOA si{}; si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessA(exe.c_str(), const_cast<char*>(cmd.data()), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  out.pi = pi;
  return ok == TRUE;
}

#include <winsock2.h>
static std::uint16_t free_port() {
  WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = 0; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(s, reinterpret_cast<sockaddr*>(&a), static_cast<int>(sizeof(a)));
  sockaddr_in o{}; int ol = static_cast<int>(sizeof(o)); getsockname(s, reinterpret_cast<sockaddr*>(&o), &ol);
  std::uint16_t port = ntohs(o.sin_port);
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

static std::string run_worker_query(std::uint16_t port, const std::string& boot) {
  auto dir = std::filesystem::temp_directory_path();
  std::string qf = (dir / ("cxl_mpreg_" + boot + ".txt")).string();
  Proc q; q.valid = spawn2(g_worker, "--port " + std::to_string(port) + " --boot " + boot + " --query --out " + qf, q);
  std::string content;
  for (int k = 0; k < 200; ++k) {
    content = read_file(qf);
    if (!content.empty()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  kill(q);
  std::error_code ec; std::filesystem::remove(qf, ec);
  return content;
}

// The worker-death handling is an in-memory transition on the coordinator;
// poll fresh queries until it is reflected.
static bool wait_query_reflects(std::uint16_t port, const std::string& base, const std::string& needle, int timeout_ms) {
  for (int i = 0; i < timeout_ms; i += 150) {
    if (run_worker_query(port, base + std::to_string(i)).find(needle) != std::string::npos) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
  }
  return false;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::cout << "usage: test_mp_regression <coordinator-exe> <worker-exe>\n"; return 2; }
  std::string coord = argv[1];
  g_worker = argv[2];
  auto dir = std::filesystem::temp_directory_path();
  const int R = 4000;

  std::uint16_t port = free_port();
  std::string resultsA = (dir / "cxl_mpreg_wA.txt").string();
  Proc c; c.valid = spawn2(coord, "--port " + std::to_string(port) + " --epoch 1", c);
  CHECK(c.valid, "coordinator spawned");
  CHECK(wait_port(port, 5000), "coordinator listening");
  Proc wa; wa.valid = spawn2(g_worker, "--port " + std::to_string(port) +
                             " --boot bootA --publish --reserve-bytes 1073741824 --consumer c0 --out " + resultsA + " --stay", wa);
  CHECK(wa.valid, "worker A spawned");
  CHECK(wait_file(resultsA, "RESERVED", R), "worker A reserved");
  std::string rid = pid_after(read_file(resultsA), "RESERVED");
  CHECK(!rid.empty(), "parsed reservation id");
  CHECK(run_worker_query(port, "q1").find("reservation " + rid + " state=RESERVED") != std::string::npos,
        "reservation held while worker A is alive");

  kill(wa);
  CHECK(wait_query_reflects(port, "qd", "reservation " + rid + " state=RELEASED", R),
        "dead worker's uncommitted reservation released");

  kill(c);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::error_code ec;
  std::filesystem::remove(resultsA, ec);
  if (failures) { std::cout << "test_mp_regression FAILURES=" << failures << "\n"; return 1; }
  std::cout << "PASS: test_mp_regression\n";
  return 0;
}
