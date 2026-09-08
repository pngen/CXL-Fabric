// ============================================================================
// CXL Fabric - central governance facade
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/accounting.hpp"
#include "cxl_fabric/core/authority.hpp"
#include "cxl_fabric/core/device.hpp"
#include "cxl_fabric/core/pool.hpp"
#include "cxl_fabric/core/region.hpp"
#include "cxl_fabric/core/reservation.hpp"
#include "cxl_fabric/core/selection.hpp"
#include "cxl_fabric/persistence/persistence.hpp"
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cxl_fabric {

// Immutable view of the governed state used for read-heavy queries. A snapshot
// is copied cheaply-ish under lock so that a long-running selection decision
// never holds the mutation lock.
struct StateSnapshot {
  CoordinatorEpoch coordinator_epoch;
  PolicyGeneration policy_generation;
  std::map<DeviceId, CxlDevice> devices;
  std::map<RegionId, CxlRegion> regions;
  std::map<PoolId, CxlPool> pools;
  std::map<ReservationId, Reservation> reservations;
  EvidenceGeneration evidence_generation;
  CapacityLine global_ledger;
};

struct AuditReport {
  bool ok = false;
  std::vector<std::string> errors;
  CapacityLine global_ledger;
};

struct SelectionOutcome {
  bool ok = false;
  Candidate chosen;               // the winning candidate
  SelectionResult result;         // structured decision + factors
  std::vector<SelectionResult> candidates;  // all evaluated, for explanation
};

struct ReserveOutcome {
  bool ok = false;
  DecisionReason reason = DecisionReason::OK;
  Reservation reservation;
};

struct ReconfigureOutcome {
  bool ok = false;
  DecisionReason reason = DecisionReason::OK;
  RegionGeneration new_generation;
};

class Fabric {
public:
  Fabric() = default;
  explicit Fabric(CoordinatorEpoch epoch, PolicyGeneration policy);

  // --- Authority / identity ---
  void set_epoch(CoordinatorEpoch epoch);
  CoordinatorEpoch epoch() const;
  void set_policy_generation(PolicyGeneration p);
  PolicyGeneration policy_generation() const;
  EvidenceGeneration evidence_generation() const;
  void advance_evidence_generation();

  // --- Device registry ---
  // Registers or updates a device. Detects duplicate/conflicting facts and
  // rejects a stale generation rather than silently overwriting current state.
  DecisionReason register_device(const CxlDevice& dev,
                                 const AuthorityContext& ctx);

  // --- Region registry ---
  DecisionReason create_region(const CxlRegion& region, const AuthorityContext& ctx);
  DecisionReason update_region(const CxlRegion& region, const AuthorityContext& ctx);
  ReconfigureOutcome reconfigure_region(const RegionId& id,
                                        const std::vector<DeviceId>& new_members,
                                        const AuthorityContext& ctx);

  // --- Pool registry ---
  DecisionReason create_pool(const CxlPool& pool, const AuthorityContext& ctx);
  DecisionReason add_pool_member(const PoolId& pool, const PoolMember& member,
                                 const AuthorityContext& ctx);
  DecisionReason remove_pool_member(const PoolId& pool, const DeviceId& device,
                                    const AuthorityContext& ctx);

  // --- Evidence / lifecycle ---
  DecisionReason mark_device_revalidation(const DeviceId& d, const AuthorityContext& ctx);
  DecisionReason mark_device_online(const DeviceId& d, const AuthorityContext& ctx);
  DecisionReason mark_region_draining(const RegionId& id, const AuthorityContext& ctx);
  DecisionReason mark_pool_draining(const PoolId& id, const AuthorityContext& ctx);
  // Rebuilds an online pool's totals from current member evidence (this is how
  // capacity shrink/expansion is incorporated; never trusts disk).
  DecisionReason reconcile_pool(const PoolId& id, const AuthorityContext& ctx);
  DecisionReason revalidate_pool(const PoolId& id, const AuthorityContext& ctx);
  DecisionReason revalidate_region(const RegionId& id, const AuthorityContext& ctx);

  // --- Admission / reservation lifecycle ---
  SelectionOutcome select(const AdmissionRequest& req) const;   // evaluate only
  ReserveOutcome reserve(const AdmissionRequest& req, const AuthorityContext& ctx);
  ReserveOutcome commit(const ReservationId& res, const AuthorityContext& ctx);
  ReserveOutcome release(const ReservationId& res, const AuthorityContext& ctx);

  // --- Failover (selection only; data movement belongs to adjacent runtimes) ---
  struct FailoverPlan { FailoverOutcome outcome; std::optional<Candidate> replacement; std::string note; };
  FailoverPlan plan_failover(const PoolId& pool, const DeviceId& failed_device,
                             const AdmissionRequest& req, const AuthorityContext& ctx) const;

  // --- Persistence export/import ---
  bool export_persisted(PersistedState& out) const;
  void import_persisted(const PersistedState& in);

  // --- Audit / queries ---
  AuditReport audit() const;
  StateSnapshot snapshot() const;
  std::size_t device_count() const;
  std::size_t region_count() const;
  std::size_t pool_count() const;
  std::size_t reservation_count() const;

private:
  void recompute_pool_locked(const std::string& pool_key, CxlPool& pool);
  void recompute_global_locked();
  SelectionOutcome build_selection(const StateSnapshot& snap, const AdmissionRequest& req) const;
  // Build the capacity-bearing view needed by selection (devices/pools/regions)
  // WITHOUT the reservation ledger, which is irrelevant to a placement decision
  // but grows monotonically. Copying the whole `state_` here makes admission
  // O(n^2) under sustained churn; selection only consults the bounded topology.
  StateSnapshot selection_snapshot_locked() const;
  const CxlDevice* find_device_locked(const DeviceId& d) const;
  const CxlRegion* find_region_locked(const RegionId& r) const;
  const CxlPool* find_pool_locked(const PoolId& p) const;

  // State + mutation lock. Read-heavy queries snapshot, so the lock is held
  // only for the briefest mutation windows. No socket I/O, backend calls,
  // callbacks, persistence, or child waits ever occur under this lock.
  mutable std::mutex mutex_;
  StateSnapshot state_;
  std::uint64_t sequence_ = 0;
};

}  // namespace cxl_fabric