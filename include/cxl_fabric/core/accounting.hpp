// ============================================================================
// CXL Fabric - exact, overflow-safe capacity accounting
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include <cstdint>
#include <string>

namespace cxl_fabric {

using ByteCount = std::uint64_t;

// Overflow-safe arithmetic: return false rather than wrap on overflow. A
// wrapped capacity count silently re-authorizes state and is never acceptable.
inline bool add_checked(ByteCount a, ByteCount b, ByteCount& out) noexcept {
  if (a > UINT64_MAX - b) return false;
  out = a + b;
  return true;
}
inline bool sub_checked(ByteCount a, ByteCount b, ByteCount& out) noexcept {
  if (b > a) return false;
  out = a - b;
  return true;
}
inline bool mul_checked(ByteCount a, ByteCount b, ByteCount& out) noexcept {
  if (a != 0 && b > UINT64_MAX / a) return false;
  out = a * b;
  return true;
}

// One governed capacity line. Invariants (audited by audit()):
//   free + unavailable + reserved + committed + draining == online
//   online <= total
//   every component is non-negative (guaranteed by type/checked ops)
struct CapacityLine {
  ByteCount total = 0;        // raw physical total
  ByteCount online = 0;       // observed online capacity
  ByteCount unavailable = 0;  // online but not allocatable (degraded/poison)
  ByteCount reserved = 0;     // reserved, not yet committed
  ByteCount committed = 0;    // reserved and committed (in use)
  ByteCount draining = 0;     // draining (fenced from new admission)

  ByteCount free() const noexcept {
    ByteCount used = unavailable + reserved + committed + draining;
    if (online <= used) return 0;
    return online - used;
  }

  // Returns true if all accounting invariants hold.
  bool audit(std::string& err) const noexcept {
    ByteCount used = unavailable + reserved + committed + draining;
    if (online > total) { err = "online > total"; return false; }
    if (used > online) { err = "used > online"; return false; }
    if (free() > online) { err = "free > online"; return false; }
    return true;
  }

  bool reserve(ByteCount n) noexcept {
    if (n == 0) return false;
    if (n > free()) return false;
    reserved += n;
    return true;
  }
  bool commit(ByteCount n) noexcept {
    if (n == 0 || n > reserved) return false;
    reserved -= n;
    ByteCount c;
    if (!add_checked(committed, n, c)) return false;
    committed = c;
    return true;
  }
  bool release(ByteCount n) noexcept {
    // release from committed first (or reserved); both are allowed paths.
    // Validate the whole operation BEFORE mutating so a failed release never
    // partially changes accounting ("no silent partial success").
    ByteCount c = committed;
    ByteCount rsv = reserved;
    ByteCount remaining = n;
    if (remaining > 0 && c > 0) {
      ByteCount take = remaining < c ? remaining : c;
      c -= take;
      remaining -= take;
    }
    if (remaining > 0) {
      if (remaining > rsv) return false;
      rsv -= remaining;
    }
    committed = c;
    reserved = rsv;
    return true;
  }
  bool fence_drain(ByteCount n) noexcept {
    if (n > free() + reserved + committed) return false;
    draining += n;
    return true;
  }
};

}  // namespace cxl_fabric