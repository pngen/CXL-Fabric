// ============================================================================
// CXL Fabric - central governance engine implementation
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#include "cxl_fabric/core/fabric.hpp"
#include "cxl_fabric/core/enum_strings.hpp"
#include "cxl_fabric/core/lifecycle.hpp"
#include <algorithm>
#include <limits>

namespace cxl_fabric {

namespace {
// Locality ordinal for deterministic ranking (lower is more local).
int locality_ordinal(LocalityClass l) {
  switch (l) {
    case LocalityClass::DIRECT_LOCAL: return 0;
    case LocalityClass::SAME_HOST: return 1;
    case LocalityClass::SAME_NUMA_DOMAIN: return 2;
    case LocalityClass::REMOTE_NUMA_DOMAIN: return 3;
    case LocalityClass::SAME_CXL_ROOT: return 4;
    case LocalityClass::SAME_SWITCH: return 5;
    case LocalityClass::SWITCH_REMOTE: return 6;
    case LocalityClass::MULTI_HOP_CXL: return 7;
    case LocalityClass::CROSS_HOST: return 8;
    case LocalityClass::UNKNOWN: return 9;
  }
  return 9;
}

ByteCount pool_free(const CxlPool& p) {
  ByteCount used = p.unavailable_bytes + p.reserved_bytes + p.committed_bytes + p.draining_bytes;
  if (p.total_governed_bytes <= used) return 0;
  return p.total_governed_bytes - used;
}
ByteCount region_free(const CxlRegion& r) {
  ByteCount used = r.reserved_bytes + r.committed_bytes;
  if (r.region_capacity_bytes <= used) return 0;
  return r.region_capacity_bytes - used;
}
bool is_active_generation(DeviceGeneration g) { return !g.empty(); }
}  // namespace

Fabric::Fabric(CoordinatorEpoch epoch, PolicyGeneration policy) {
  state_.coordinator_epoch = epoch;
  state_.policy_generation = policy;
  state_.global_ledger = CapacityLine{};
}

void Fabric::set_epoch(CoordinatorEpoch e) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.coordinator_epoch = e;
}
CoordinatorEpoch Fabric::epoch() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.coordinator_epoch;
}
void Fabric::set_policy_generation(PolicyGeneration p) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.policy_generation = p;
}
PolicyGeneration Fabric::policy_generation() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.policy_generation;
}
EvidenceGeneration Fabric::evidence_generation() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.evidence_generation;
}
void Fabric::advance_evidence_generation() {
  std::lock_guard<std::mutex> lock(mutex_);
  auto n = state_.evidence_generation.next();
  state_.evidence_generation = n.empty() ? state_.evidence_generation : n;
}

const CxlDevice* Fabric::find_device_locked(const DeviceId& d) const {
  auto it = state_.devices.find(d);
  return it == state_.devices.end() ? nullptr : &it->second;
}
const CxlRegion* Fabric::find_region_locked(const RegionId& r) const {
  auto it = state_.regions.find(r);
  return it == state_.regions.end() ? nullptr : &it->second;
}
const CxlPool* Fabric::find_pool_locked(const PoolId& p) const {
  auto it = state_.pools.find(p);
  return it == state_.pools.end() ? nullptr : &it->second;
}

void Fabric::recompute_global_locked() {
  // Rebuild the global ledger exactly from every non-retired pool, summing the
  // per-pool accounting fields so reserved/committed/free always close.
  CapacityLine g;
  std::uint64_t total_physical = 0;
  for (const auto& kv : state_.pools) {
    const CxlPool& p = kv.second;
    if (p.state == PoolState::RETIRED) continue;
    ByteCount t;
    if (!add_checked(g.total, p.total_governed_bytes, t)) continue;
    g.total = t;
    if (!add_checked(g.online, p.total_governed_bytes, t)) continue;
    g.online = t;
    if (!add_checked(g.unavailable, p.unavailable_bytes, t)) continue;
    g.unavailable = t;
    if (!add_checked(g.reserved, p.reserved_bytes, t)) continue;
    g.reserved = t;
    if (!add_checked(g.committed, p.committed_bytes, t)) continue;
    g.committed = t;
    if (!add_checked(g.draining, p.draining_bytes, t)) continue;
    g.draining = t;
    total_physical = t;
  }
  state_.global_ledger = g;
}

// Recompute a pool's governed totals from its members.
void Fabric::recompute_pool_locked(const std::string&, CxlPool& pool) {
  ByteCount total = 0, unavailable = 0;
  std::vector<std::string> domains;
  for (const auto& m : pool.members) {
    if (!m.online) continue;
    ByteCount t;
    if (add_checked(total, m.nominal_bytes, t)) total = t;
    if (!m.health_ok) {
      ByteCount u;
      if (add_checked(unavailable, m.nominal_bytes, u)) unavailable = u;
    }
    if (!m.failure_domain.empty()) {
      bool seen = false;
      for (const auto& d : domains) if (d == m.failure_domain) { seen = true; break; }
      if (!seen) domains.push_back(m.failure_domain);
    }
  }
  pool.total_governed_bytes = total;
  pool.unavailable_bytes = unavailable;
  pool.failure_domains = std::move(domains);
}

DecisionReason Fabric::register_device(const CxlDevice& dev, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch)
    return DecisionReason::RESERVATION_STALE;  // stale epoch fenced
  if (dev.id.empty() || dev.generation.empty() || dev.boot_id.empty())
    return DecisionReason::INVALID_ARGUMENT;
  auto it = state_.devices.find(dev.id);
  if (it == state_.devices.end()) {
    state_.devices.emplace(dev.id, dev);
    return DecisionReason::OK;
  }
  CxlDevice& existing = it->second;
  // Conflicting immutable facts at the same generation are rejected.
  if (existing.generation == dev.generation) {
    if (existing.vendor_device_id != dev.vendor_device_id)
      return DecisionReason::CONFLICTING_FACTS;
    if (existing.total_physical_bytes != dev.total_physical_bytes)
      return DecisionReason::CONFLICTING_FACTS;
    existing = dev;  // refresh observations
    return DecisionReason::OK;
  }
  // A stale generation must never overwrite current state.
  if (dev.generation < existing.generation)
    return DecisionReason::DEVICE_STALE;
  // New generation supersedes: allow the new record to advance authority.
  if (!valid_transition(existing.state, dev.state))
    return DecisionReason::INVALID_ARGUMENT;
  existing = dev;
  return DecisionReason::OK;
}

DecisionReason Fabric::create_region(const CxlRegion& region, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch)
    return DecisionReason::RESERVATION_STALE;
  if (region.id.empty() || region.generation.empty() || region.pool.empty())
    return DecisionReason::INVALID_ARGUMENT;
  if (state_.regions.count(region.id)) return DecisionReason::DUPLICATE_IDENTITY;
  if (!state_.pools.count(region.pool)) return DecisionReason::POOL_STALE;
  const CxlPool& pool = state_.pools.at(region.pool);
  for (const auto& d : region.member_devices) {
    if (!state_.devices.count(d)) return DecisionReason::DEVICE_NOT_ONLINE;
    // Member devices should belong to the owning pool.
    bool in_pool = false;
    for (const auto& m : pool.members) if (m.device == d) { in_pool = true; break; }
    if (!in_pool) return DecisionReason::CONFLICTING_FACTS;
  }
  state_.regions.emplace(region.id, region);
  return DecisionReason::OK;
}

DecisionReason Fabric::update_region(const CxlRegion& region, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch)
    return DecisionReason::RESERVATION_STALE;
  auto it = state_.regions.find(region.id);
  if (it == state_.regions.end()) return DecisionReason::REGION_STALE;
  if (region.generation < it->second.generation) return DecisionReason::REGION_STALE;
  it->second = region;
  return DecisionReason::OK;
}

ReconfigureOutcome Fabric::reconfigure_region(const RegionId& id,
                                              const std::vector<DeviceId>& new_members,
                                              const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  ReconfigureOutcome out;
  if (ctx.coordinator_epoch != state_.coordinator_epoch) {
    out.reason = DecisionReason::RESERVATION_STALE; return out;
  }
  auto it = state_.regions.find(id);
  if (it == state_.regions.end()) { out.reason = DecisionReason::REGION_STALE; return out; }
  auto ng = it->second.generation.next();
  if (ng.empty()) { out.reason = DecisionReason::INVALID_ARGUMENT; return out; }
  it->second.generation = ng;
  it->second.member_devices = new_members;
  it->second.state = RegionState::RECONFIGURING;
  it->second.evidence_current = false;   // must revalidate after reconfiguration
  state_.global_ledger = state_.global_ledger;  // no capacity change yet
  out.ok = true;
  out.new_generation = ng;
  return out;
}

DecisionReason Fabric::create_pool(const CxlPool& pool, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch)
    return DecisionReason::RESERVATION_STALE;
  if (pool.id.empty() || pool.generation.empty()) return DecisionReason::INVALID_ARGUMENT;
  if (state_.pools.count(pool.id)) return DecisionReason::DUPLICATE_IDENTITY;
  std::vector<DeviceId> seen;
  for (const auto& m : pool.members) {
    if (!state_.devices.count(m.device)) return DecisionReason::DEVICE_NOT_ONLINE;
    for (const auto& d : seen) if (d == m.device) return DecisionReason::DUPLICATE_IDENTITY;
    seen.push_back(m.device);
    // A device may only be governed by one pool at a time (no double counting).
    for (const auto& kv : state_.pools) {
      for (const auto& pm : kv.second.members)
        if (pm.device == m.device) return DecisionReason::CONFLICTING_FACTS;
    }
  }
  CxlPool copy = pool;
  recompute_pool_locked(copy.id.str(), copy);
  copy.state = PoolState::AVAILABLE;
  copy.evidence_current = true;
  state_.pools.emplace(copy.id, copy);
  recompute_global_locked();
  return DecisionReason::OK;
}

DecisionReason Fabric::add_pool_member(const PoolId& pool_id, const PoolMember& member,
                                       const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch)
    return DecisionReason::RESERVATION_STALE;
  auto it = state_.pools.find(pool_id);
  if (it == state_.pools.end()) return DecisionReason::POOL_STALE;
  if (!state_.devices.count(member.device)) return DecisionReason::DEVICE_NOT_ONLINE;
  if (member.device_generation.empty()) return DecisionReason::INVALID_ARGUMENT;
  for (const auto& pm : it->second.members)
    if (pm.device == member.device) return DecisionReason::DUPLICATE_IDENTITY;
  for (const auto& kv : state_.pools) {
    if (kv.first == pool_id) continue;
    for (const auto& pm : kv.second.members)
      if (pm.device == member.device) return DecisionReason::CONFLICTING_FACTS;
  }
  it->second.members.push_back(member);
  auto ng = it->second.generation.next();
  it->second.generation = ng.empty() ? it->second.generation : ng;
  recompute_pool_locked(it->second.id.str(), it->second);
  recompute_global_locked();
  return DecisionReason::OK;
}

DecisionReason Fabric::remove_pool_member(const PoolId& pool_id, const DeviceId& device,
                                          const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch)
    return DecisionReason::RESERVATION_STALE;
  auto it = state_.pools.find(pool_id);
  if (it == state_.pools.end()) return DecisionReason::POOL_STALE;
  auto& members = it->second.members;
  auto it2 = std::find_if(members.begin(), members.end(), [&](const PoolMember& m){ return m.device == device; });
  if (it2 == members.end()) return DecisionReason::DEVICE_NOT_ONLINE;
  members.erase(it2);
  auto ng = it->second.generation.next();
  it->second.generation = ng.empty() ? it->second.generation : ng;
  recompute_pool_locked(it->second.id.str(), it->second);
  recompute_global_locked();
  return DecisionReason::OK;
}

DecisionReason Fabric::mark_device_revalidation(const DeviceId& d, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch)
    return DecisionReason::RESERVATION_STALE;
  auto it = state_.devices.find(d);
  if (it == state_.devices.end()) return DecisionReason::DEVICE_NOT_ONLINE;
  if (!valid_transition(it->second.state, DeviceState::REVALIDATION_REQUIRED))
    return DecisionReason::INVALID_ARGUMENT;
  it->second.state = DeviceState::REVALIDATION_REQUIRED;
  it->second.evidence_current = false;
  return DecisionReason::OK;
}
DecisionReason Fabric::mark_device_online(const DeviceId& d, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch)
    return DecisionReason::RESERVATION_STALE;
  auto it = state_.devices.find(d);
  if (it == state_.devices.end()) return DecisionReason::DEVICE_NOT_ONLINE;
  if (!valid_transition(it->second.state, DeviceState::ONLINE))
    return DecisionReason::INVALID_ARGUMENT;
  it->second.state = DeviceState::ONLINE;
  it->second.evidence_current = true;
  return DecisionReason::OK;
}
DecisionReason Fabric::mark_region_draining(const RegionId& id, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch)
    return DecisionReason::RESERVATION_STALE;
  auto it = state_.regions.find(id);
  if (it == state_.regions.end()) return DecisionReason::REGION_STALE;
  if (!valid_transition(it->second.state, RegionState::DRAINING))
    return DecisionReason::INVALID_ARGUMENT;
  it->second.state = RegionState::DRAINING;
  return DecisionReason::OK;
}
DecisionReason Fabric::mark_pool_draining(const PoolId& id, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch)
    return DecisionReason::RESERVATION_STALE;
  auto it = state_.pools.find(id);
  if (it == state_.pools.end()) return DecisionReason::POOL_STALE;
  if (!valid_transition(it->second.state, PoolState::DRAINING))
    return DecisionReason::INVALID_ARGUMENT;
  it->second.state = PoolState::DRAINING;
  return DecisionReason::OK;
}

DecisionReason Fabric::reconcile_pool(const PoolId& id, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch)
    return DecisionReason::RESERVATION_STALE;
  auto it = state_.pools.find(id);
  if (it == state_.pools.end()) return DecisionReason::POOL_STALE;
  recompute_pool_locked(it->second.id.str(), it->second);
  recompute_global_locked();
  return DecisionReason::OK;
}

SelectionOutcome Fabric::build_selection(const StateSnapshot& snap, const AdmissionRequest& req) const {
  SelectionOutcome out;
  std::vector<Candidate> selected;
  for (const auto& pk : snap.pools) {
    const CxlPool& pool = pk.second;
    if (!pool.evidence_current) continue;
    if (pool.state != PoolState::AVAILABLE && pool.state != PoolState::DEFINED) continue;
    for (const auto& rk : snap.regions) {
      const CxlRegion& region = rk.second;
      if (region.pool != pool.id) continue;
      if (!region.evidence_current) continue;
      if (region.state != RegionState::ONLINE) continue;
      for (const auto& dev_id : region.member_devices) {
        auto dit = snap.devices.find(dev_id);
        if (dit == snap.devices.end()) continue;
        const CxlDevice& dev = dit->second;
        if (!dev.evidence_current) continue;
        if (dev.state != DeviceState::ONLINE) continue;
        Candidate c;
        c.pool = pool.id; c.pool_generation = pool.generation;
        c.region = region.id; c.region_generation = region.generation;
        c.device = dev.id; c.device_generation = dev.generation;
        c.available_bytes = region_free(region);
        c.device_record = &dev;
        c.region_record = &region;
        c.pool_record = &pool;
        SelectionResult r = evaluate_candidate(c, req, pool.evidence_current);
        out.candidates.push_back(r);
        if (r.selected) out.candidates.back().explanation = "candidate passed";
        if (r.selected) selected.push_back(c);
      }
    }
  }
  // Deterministic ranking: locality, latency, bandwidth, capacity, then stable id.
  std::stable_sort(selected.begin(), selected.end(), [](const Candidate& a, const Candidate& b) {
    const CxlDevice& da = *a.device_record;
    const CxlDevice& db = *b.device_record;
    int la = locality_ordinal(da.economics.locality);
    int lb = locality_ordinal(db.economics.locality);
    if (la != lb) return la < lb;
    if (da.economics.estimated_latency_ns != db.economics.estimated_latency_ns)
      return da.economics.estimated_latency_ns < db.economics.estimated_latency_ns;
    if (da.economics.estimated_bandwidth_mbps != db.economics.estimated_bandwidth_mbps)
      return da.economics.estimated_bandwidth_mbps > db.economics.estimated_bandwidth_mbps;
    if (a.available_bytes != b.available_bytes)
      return a.available_bytes > b.available_bytes;
    return a.pool.str() + a.region.str() + a.device.str() <
           b.pool.str() + b.region.str() + b.device.str();
  });
  if (!selected.empty()) {
    out.chosen = selected.front();
    out.ok = true;
    out.result = out.candidates.front();  // placeholder; set below
    // Restore the matching result for the chosen candidate.
    for (auto& c : out.candidates) {
      if (c.pool == out.chosen.pool.str() && c.region == out.chosen.region.str() &&
          c.device == out.chosen.device.str() && c.selected) { out.result = c; break; }
    }
    return out;
  }
  // No candidate passed hardness. Report the dominant rejection (or a
  // meaningful reason when there were no candidates at all).
  if (!out.candidates.empty()) {
    out.result = out.candidates.front();
    out.result.selected = false;
    if (out.result.reason != DecisionReason::OK) out.ok = false;
  } else {
    out.result.selected = false;
    out.result.reason = DecisionReason::CAPACITY_INSUFFICIENT;
    out.ok = false;
  }
  return out;
}

SelectionOutcome Fabric::select(const AdmissionRequest& req) const {
  std::lock_guard<std::mutex> lock(mutex_);
  StateSnapshot snap = selection_snapshot_locked();
  return build_selection(snap, req);
}

ReserveOutcome Fabric::reserve(const AdmissionRequest& req, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch) {
    ReserveOutcome o; o.reason = DecisionReason::RESERVATION_STALE; return o;
  }
  if (req.bytes == 0) {
    ReserveOutcome o; o.reason = DecisionReason::INVALID_ARGUMENT; return o;
  }
  StateSnapshot snap = selection_snapshot_locked();  // capacity view only
  SelectionOutcome sel = build_selection(snap, req);
  if (!sel.ok) {
    ReserveOutcome o; o.reason = sel.result.reason; return o;
  }
  const Candidate& c = sel.chosen;
  auto pit = state_.pools.find(c.pool);
  auto rit = state_.regions.find(c.region);
  auto dit = state_.devices.find(c.device);
  if (pit == state_.pools.end() || rit == state_.regions.end() || dit == state_.devices.end()) {
    ReserveOutcome o; o.reason = DecisionReason::UNKNOWN_ERROR; return o;
  }
  // Re-validate generations in the *current* state under lock.
  if (pit->second.generation != c.pool_generation ||
      rit->second.generation != c.region_generation ||
      dit->second.generation != c.device_generation) {
    ReserveOutcome o; o.reason = DecisionReason::POOL_GENERATION_STALE; return o;
  }
  CxlPool& pool = pit->second;
  CxlRegion& region = rit->second;
  CxlDevice& dev = dit->second;
  ByteCount pf = pool_free(pool);
  ByteCount rf = region_free(region);
  if (req.bytes > pf || req.bytes > rf) {
    ReserveOutcome o; o.reason = DecisionReason::CAPACITY_OVERCOMMIT; return o;
  }
  pool.reserved_bytes += req.bytes;
  region.reserved_bytes += req.bytes;
  recompute_global_locked();
  Reservation res;
  res.id = ReservationId{"res-" + std::to_string(++sequence_)};
  res.consumer = req.consumer;
  res.consumer_generation = ctx.consumer_generation;
  res.pool = c.pool;
  res.pool_generation = c.pool_generation;
  res.region = c.region;
  res.region_generation = c.region_generation;
  res.device = c.device;
  res.device_generation = c.device_generation;
  res.device_boot_id = dev.boot_id;
  res.bytes = req.bytes;
  res.state = ResumeState::RESERVED;
  res.created_epoch = state_.coordinator_epoch;
  res.worker_boot_id = ctx.worker_boot_id;
  res.evidence_current_at_reserve = dev.evidence_current;
  state_.reservations.emplace(res.id, res);
  ReserveOutcome o; o.ok = true; o.reason = DecisionReason::OK; o.reservation = res;
  return o;
}

ReserveOutcome Fabric::commit(const ReservationId& res_id, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  ReserveOutcome o;
  auto it = state_.reservations.find(res_id);
  if (it == state_.reservations.end()) { o.reason = DecisionReason::RESERVATION_STALE; return o; }
  Reservation& res = it->second;
  if (res.state != ResumeState::RESERVED) { o.reason = DecisionReason::RESERVATION_STALE; return o; }
  if (ctx.coordinator_epoch != state_.coordinator_epoch) { o.reason = DecisionReason::RESERVATION_STALE; return o; }
  // A reservation created under an older epoch cannot commit against the new epoch.
  if (res.created_epoch != state_.coordinator_epoch) { o.reason = DecisionReason::RESERVATION_STALE; return o; }
  // A reservation created by a worker may only be committed by that same worker
  // incarnation. A dead/replaced worker carries a different WorkerBootId and is
  // therefore fenced: new capacity commitments never depend on stale evidence.
  if (!ctx.worker_boot_id.empty() && ctx.worker_boot_id != res.worker_boot_id) {
    o.reason = DecisionReason::RESERVATION_STALE; return o;
  }
  auto pit = state_.pools.find(res.pool);
  auto rit = state_.regions.find(res.region);
  auto dit = state_.devices.find(res.device);
  if (pit == state_.pools.end() || rit == state_.regions.end() || dit == state_.devices.end()) {
    o.reason = DecisionReason::POOL_GENERATION_STALE; return o;
  }
  // Membership/pool generation change fends old reservations.
  if (pit->second.generation != res.pool_generation) { o.reason = DecisionReason::POOL_GENERATION_STALE; return o; }
  if (rit->second.generation != res.region_generation) { o.reason = DecisionReason::REGION_STALE; return o; }
  if (dit->second.generation != res.device_generation) { o.reason = DecisionReason::DEVICE_STALE; return o; }
  if (res.bytes > pit->second.reserved_bytes) { o.reason = DecisionReason::CAPACITY_OVERCOMMIT; return o; }
  pit->second.reserved_bytes -= res.bytes;
  pit->second.committed_bytes += res.bytes;
  rit->second.reserved_bytes -= res.bytes;
  rit->second.committed_bytes += res.bytes;
  res.state = ResumeState::COMMITTED;
  res.commit_epoch = state_.coordinator_epoch;
  recompute_global_locked();
  o.ok = true; o.reason = DecisionReason::OK; o.reservation = res;
  return o;
}

ReserveOutcome Fabric::release(const ReservationId& res_id, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  ReserveOutcome o;
  auto it = state_.reservations.find(res_id);
  if (it == state_.reservations.end()) { o.reason = DecisionReason::RESERVATION_STALE; return o; }
  Reservation& res = it->second;
  if (res.state == ResumeState::RELEASED) { o.reason = DecisionReason::RESERVATION_STALE; return o; }
  if (ctx.coordinator_epoch != state_.coordinator_epoch) { o.reason = DecisionReason::RESERVATION_STALE; return o; }
  if (res.state != ResumeState::COMMITTED && res.state != ResumeState::RESERVED) {
    o.reason = DecisionReason::RESERVATION_STALE; return o;
  }
  // Release from the pool then the region, never going negative and never
  // leaving a remainder (which would indicate a corrupt accounting mismatch).
  auto pit = state_.pools.find(res.pool);
  auto rit = state_.regions.find(res.region);
  if (pit != state_.pools.end()) {
    ByteCount remaining = res.bytes;
    if (remaining > 0 && pit->second.committed_bytes > 0) {
      ByteCount take = remaining < pit->second.committed_bytes ? remaining : pit->second.committed_bytes;
      pit->second.committed_bytes -= take;
      remaining -= take;
    }
    if (remaining > 0 && remaining <= pit->second.reserved_bytes) {
      pit->second.reserved_bytes -= remaining;
      remaining = 0;
    }
    if (remaining != 0) { o.reason = DecisionReason::RESERVATION_CONFLICT; return o; }
  }
  if (rit != state_.regions.end()) {
    ByteCount remaining = res.bytes;
    if (remaining > 0 && rit->second.committed_bytes > 0) {
      ByteCount take = remaining < rit->second.committed_bytes ? remaining : rit->second.committed_bytes;
      rit->second.committed_bytes -= take;
      remaining -= take;
    }
    if (remaining > 0 && remaining <= rit->second.reserved_bytes) {
      rit->second.reserved_bytes -= remaining;
      remaining = 0;
    }
  }
  res.state = ResumeState::RELEASED;
  recompute_global_locked();
  o.ok = true; o.reason = DecisionReason::OK; o.reservation = res;
  return o;
}

Fabric::FailoverPlan Fabric::plan_failover(const PoolId& pool_id, const DeviceId& failed_device,
                                           const AdmissionRequest& req,
                                           const AuthorityContext& ctx) const {
  std::lock_guard<std::mutex> lock(mutex_);
  (void)ctx;  // failover planning is read-only; authority is validated by the caller
  FailoverPlan plan;
  std::string failed_domain;
  for (const auto& kv : state_.pools) {
    for (const auto& m : kv.second.members) if (m.device == failed_device) failed_domain = m.failure_domain;
  }
  StateSnapshot snap = state_;
  // Prefer a replacement outside the failed device's failure domain. That is
  // authority to *use* a replacement target; moving data into it belongs to an
  // adjacent movement runtime, never to CXL Fabric.
  std::vector<Candidate> cross_domain;
  std::vector<Candidate> same_domain;
  for (const auto& pk : snap.pools) {
    if (!pool_id.empty() && pk.first != pool_id) continue;
    const CxlPool& p = pk.second;
    if (!p.evidence_current) continue;
    for (const auto& rk : snap.regions) {
      if (rk.second.pool != p.id) continue;
      if (!rk.second.evidence_current || rk.second.state != RegionState::ONLINE) continue;
      for (const auto& did : rk.second.member_devices) {
        if (did == failed_device) continue;
        auto dit = snap.devices.find(did);
        if (dit == snap.devices.end() || !dit->second.evidence_current) continue;
        if (dit->second.state != DeviceState::ONLINE) continue;
        Candidate c;
        c.pool = p.id; c.pool_generation = p.generation;
        c.region = rk.second.id; c.region_generation = rk.second.generation;
        c.device = did; c.device_generation = dit->second.generation;
        c.available_bytes = region_free(rk.second);
        c.device_record = &dit->second;
        c.region_record = &rk.second;
        c.pool_record = &p;
        SelectionResult r = evaluate_candidate(c, req, p.evidence_current);
        if (!r.selected) continue;
        bool same = (!failed_domain.empty() && dit->second.economics.notes == failed_domain);
        (same ? same_domain : cross_domain).push_back(c);
      }
    }
  }
  Candidate* replacement = nullptr;
  if (!cross_domain.empty()) {
    replacement = &cross_domain.front();
    plan.outcome = FailoverOutcome::FAILOVER;
    plan.note = "cross-domain replacement found";
  } else if (!same_domain.empty()) {
    replacement = &same_domain.front();
    plan.outcome = FailoverOutcome::DEGRADED;
    plan.note = "only same-failure-domain replacement available -> degraded fallback";
  } else {
    plan.outcome = FailoverOutcome::REJECT;
    plan.note = "no replacement candidate satisfies the request";
  }
  if (replacement) plan.replacement = *replacement;
  return plan;
}

std::size_t Fabric::device_count() const {
  std::lock_guard<std::mutex> lock(mutex_); return state_.devices.size();
}
std::size_t Fabric::region_count() const {
  std::lock_guard<std::mutex> lock(mutex_); return state_.regions.size();
}
std::size_t Fabric::pool_count() const {
  std::lock_guard<std::mutex> lock(mutex_); return state_.pools.size();
}
std::size_t Fabric::reservation_count() const {
  std::lock_guard<std::mutex> lock(mutex_); return state_.reservations.size();
}

bool Fabric::export_persisted(PersistedState& out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  out.policy_generation = state_.policy_generation;
  out.devices = state_.devices;
  out.regions = state_.regions;
  out.pools = state_.pools;
  return true;
}

void Fabric::import_persisted(const PersistedState& in) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.policy_generation = in.policy_generation;
  state_.devices = in.devices;
  state_.regions = in.regions;
  state_.pools = in.pools;
  // Reservations are never restored: they are fenced by absence after a
  // coordinator restart. Dynamic evidence is never CURRENT from disk.
  state_.reservations.clear();
  for (auto& kv : state_.devices) {
    kv.second.evidence_current = false;
    kv.second.state = DeviceState::REVALIDATION_REQUIRED;
    kv.second.health = HealthState::UNKNOWN;
  }
  for (auto& kv : state_.regions) {
    kv.second.evidence_current = false;
    kv.second.state = RegionState::REVALIDATION_REQUIRED;
  }
  for (auto& kv : state_.pools) {
    kv.second.evidence_current = false;
    kv.second.state = PoolState::REVALIDATION_REQUIRED;
  }
  recompute_global_locked();
}

DecisionReason Fabric::revalidate_pool(const PoolId& id, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch) return DecisionReason::RESERVATION_STALE;
  auto it = state_.pools.find(id);
  if (it == state_.pools.end()) return DecisionReason::POOL_STALE;
  if (!valid_transition(it->second.state, PoolState::AVAILABLE)) return DecisionReason::INVALID_ARGUMENT;
  it->second.state = PoolState::AVAILABLE;
  it->second.evidence_current = true;
  it->second.health_summary = HealthState::HEALTHY;
  recompute_pool_locked(it->second.id.str(), it->second);
  recompute_global_locked();
  return DecisionReason::OK;
}
DecisionReason Fabric::revalidate_region(const RegionId& id, const AuthorityContext& ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ctx.coordinator_epoch != state_.coordinator_epoch) return DecisionReason::RESERVATION_STALE;
  auto it = state_.regions.find(id);
  if (it == state_.regions.end()) return DecisionReason::REGION_STALE;
  if (!valid_transition(it->second.state, RegionState::ONLINE)) return DecisionReason::INVALID_ARGUMENT;
  it->second.state = RegionState::ONLINE;
  it->second.evidence_current = true;
  it->second.health = HealthState::HEALTHY;
  return DecisionReason::OK;
}

AuditReport Fabric::audit() const {
  std::lock_guard<std::mutex> lock(mutex_);
  AuditReport rep;
  std::string err;
  std::vector<std::string> errors;
  CapacityLine g;
  // Recompute sum of all pools to validate exact closure independently.
  for (const auto& kv : state_.pools) {
    const CxlPool& p = kv.second;
    if (p.state == PoolState::RETIRED) continue;
    ByteCount t;
    if (add_checked(g.total, p.total_governed_bytes, t)) g.total = t;
    if (add_checked(g.online, p.total_governed_bytes, t)) g.online = t;
    if (add_checked(g.unavailable, p.unavailable_bytes, t)) g.unavailable = t;
    if (add_checked(g.reserved, p.reserved_bytes, t)) g.reserved = t;
    if (add_checked(g.committed, p.committed_bytes, t)) g.committed = t;
    if (add_checked(g.draining, p.draining_bytes, t)) g.draining = t;
    if (p.total_governed_bytes > 0 && !p.unavailable_bytes && !p.reserved_bytes &&
        !p.committed_bytes && !p.draining_bytes) {
      // free equals total for an idle pool; nothing special to note
    }
    if (p.total_governed_bytes < p.unavailable_bytes + p.reserved_bytes +
                                       p.committed_bytes + p.draining_bytes) {
      errors.push_back("pool " + p.id.str() + " used > governed");
    }
  }
  if (!g.audit(err)) errors.push_back("global ledger: " + err);
  g.audit(err);
  rep.global_ledger = g;
  rep.ok = errors.empty();
  rep.errors = std::move(errors);
  return rep;
}

StateSnapshot Fabric::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

StateSnapshot Fabric::selection_snapshot_locked() const {
  // Selection evaluates capacity-bearing records only (devices/pools/regions).
  // The reservation ledger is excluded: it grows monotonically as reservations
  // are reserved/committed/released and never factors into a placement decision,
  // so copying it would turn every admission into a deep copy of the whole
  // history (O(n^2) under churn). Callers hold the lock.
  StateSnapshot snap;
  snap.coordinator_epoch = state_.coordinator_epoch;
  snap.policy_generation = state_.policy_generation;
  snap.evidence_generation = state_.evidence_generation;
  snap.global_ledger = state_.global_ledger;
  snap.devices = state_.devices;
  snap.regions = state_.regions;
  snap.pools = state_.pools;
  return snap;
}

}  // namespace cxl_fabric
