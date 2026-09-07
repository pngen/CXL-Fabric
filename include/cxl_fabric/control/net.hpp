// ============================================================================
// CXL Fabric - loopback TCP transport for the multiprocess control plane
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/protocol/protocol.hpp"
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

namespace cxl_fabric {
namespace net {

struct Socket {
#ifdef _WIN32
  SOCKET s = INVALID_SOCKET;
  bool valid() const { return s != INVALID_SOCKET; }
  void close() { if (valid()) { closesocket(s); s = INVALID_SOCKET; } }
#else
  int s = -1;
  bool valid() const { return s >= 0; }
  void close() { if (valid()) { ::close(s); s = -1; } }
#endif
};

inline bool init() {
#ifdef _WIN32
  static WSADATA wsa;
  static bool once = false;
  if (!once) { WSAStartup(MAKEWORD(2, 2), &wsa); once = true; }
#endif
  return true;
}

inline void fini() {
#ifdef _WIN32
  WSACleanup();
#endif
}

inline bool listen_on(std::uint16_t port, Socket& out) {
  init();
#ifdef _WIN32
  SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (ls == INVALID_SOCKET) return false;
  BOOL reuse = TRUE;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse), sizeof(reuse));
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { closesocket(ls); return false; }
  if (listen(ls, 16) == SOCKET_ERROR) { closesocket(ls); return false; }
  out.s = ls;
#else
  int ls = socket(AF_INET, SOCK_STREAM, 0);
  if (ls < 0) return false;
  int reuse = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { ::close(ls); return false; }
  if (listen(ls, 16) < 0) { ::close(ls); return false; }
  out.s = ls;
#endif
  return true;
}

inline bool accept_socket(const Socket& listener, Socket& out) {
#ifdef _WIN32
  SOCKET a = accept(listener.s, nullptr, nullptr);
  if (a == INVALID_SOCKET) return false;
  out.s = a;
#else
  int a = accept(listener.s, nullptr, nullptr);
  if (a < 0) return false;
  out.s = a;
#endif
  return true;
}

inline bool connect_to(const std::string& host, std::uint16_t port, Socket& out) {
  init();
#ifdef _WIN32
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
  inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { closesocket(s); return false; }
  out.s = s;
#else
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return false;
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
  inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { ::close(s); return false; }
  out.s = s;
#endif
  return true;
}

// Send all bytes (loops on partial sends). Returns false on error.
inline bool send_all(const Socket& sock, const std::uint8_t* data, std::size_t len) {
  std::size_t sent = 0;
  while (sent < len) {
#ifdef _WIN32
    int n = ::send(sock.s, reinterpret_cast<const char*>(data + sent), int(len - sent), 0);
#else
    int n = ::send(sock.s, data + sent, len - sent, 0);
#endif
    if (n <= 0) return false;
    sent += std::size_t(n);
  }
  return true;
}

// Receive exactly `len` bytes. Returns false on error/peer-close.
inline bool recv_exact(const Socket& sock, std::uint8_t* data, std::size_t len) {
  std::size_t got = 0;
  while (got < len) {
#ifdef _WIN32
    int n = ::recv(sock.s, reinterpret_cast<char*>(data + got), int(len - got), 0);
#else
    int n = ::recv(sock.s, data + got, len - got, 0);
#endif
    if (n <= 0) return false;
    got += std::size_t(n);
  }
  return true;
}

inline bool send_frame(const Socket& sock, const Frame& f) {
  std::vector<std::uint8_t> bytes = encode_frame(f);
  return send_all(sock, bytes.data(), bytes.size());
}

// Receive one frame. Reads the 16-byte header, then the payload.
inline DecodeResult recv_frame(const Socket& sock) {
  std::uint8_t hdr[kFrameHeaderSize];
  if (!recv_exact(sock, hdr, kFrameHeaderSize)) {
    DecodeResult r; r.status = DecodeStatus::TRUNCATED; r.error = "peer closed or header truncated"; return r;
  }
  std::uint32_t len = (std::uint32_t(hdr[8]) << 24) | (std::uint32_t(hdr[9]) << 16) |
                      (std::uint32_t(hdr[10]) << 8) | std::uint32_t(hdr[11]);
  if (len > kMaxPayloadBytes) {
    DecodeResult r; r.status = DecodeStatus::OVERSIZED; r.error = "oversized frame"; return r;
  }
  std::vector<std::uint8_t> buf(kFrameHeaderSize + std::size_t(len));
  std::memcpy(buf.data(), hdr, kFrameHeaderSize);
  if (len > 0 && !recv_exact(sock, buf.data() + kFrameHeaderSize, std::size_t(len))) {
    DecodeResult r; r.status = DecodeStatus::TRUNCATED; r.error = "payload truncated"; return r;
  }
  return decode_frame(buf.data(), buf.size());
}

}  // namespace net
}  // namespace cxl_fabric
