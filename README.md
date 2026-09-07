# CXL Fabric

**CXL Fabric** is a production-quality, vendor-neutral C++20 runtime boundary for governing
**CXL-class memory infrastructure** across discovery, device identity, capacity, pooling,
locality, tiering, access eligibility, attachment, health, failover, generation-bound
authority, recovery, and accelerator-access economics.

It answers one systems question:

> Which CXL memory capacity exists now, where is it attached, which consumers may safely
> access it, under what locality, capability, health, generation, and authority, and when
> should the system use, avoid, drain, fail over, or revalidate that capacity?

CXL Fabric is **not** a generic memory allocator, a fake CXL simulator presented as physical
hardware, a NUMA library, a PCIe topology library, a GPU memory manager, a CXL protocol
implementation, a fabric manager for hardware switches, or a generic expanded-memory
abstraction. It is a **runtime boundary around authoritative CXL-class memory knowledge and
policy.**

## Why CXL capacity needs a runtime boundary

CXL capacity is governed infrastructure state, not merely a free-byte count. Usability depends
on far more than presence. The decisive rule:

> CXL capacity may be present. A region may be configured. A pool may report free bytes.
> None of those facts alone proves that a particular consumer may safely and authoritatively
> use those bytes now.

A CXL candidate depends on device/generation/incarnation identity, host attachment, region and
pool generation, decoder/window configuration, online state, health, poison/error state,
access capability, consumer compatibility, locality, NUMA/PCIe/CXL hierarchy, latency and
bandwidth class, sharing mode, pool membership, reservation state, persistence and volatility,
hotplug state, interleave configuration, coherence mode, evidence freshness, coordinator
epoch, worker boot identity, and policy generation. UNKNOWN is not permission. PRESENT is not
ONLINE. ONLINE is not necessarily ELIGIBLE. CAPACITY exists does not mean it is safely
allocatable. Persisted region metadata does not make physical capacity current after restart.
A synthetic CXL Type-3 device is not real CXL hardware.

## Exact ownership boundary

| CXL Fabric owns | CXL Fabric does NOT own |
|---|---|
| CXL memory-device identity and generation | Generic host DRAM allocation |
| Device boot/incarnation identity | Generic GPU VRAM allocation |
| Host, root, port/switch/endpoint references | GPU Memory Service semantics |
| CXL capacity, region and decoder identity | Unified Buffer semantics |
| Device-local and pooled capacity | FlashTier tier-movement semantics |
| Region/Pool identity, membership, generation | Memory Expansion Fabric expanded-memory abstraction |
| Capacity state, ownership, consumer attachments | NUMA topology ownership |
| Access eligibility, capability & locality evidence | PCIe/NVLink topology ownership |
| Health, drain, degradation, failover, revalidation | RDMA buffer registration / transfer scheduling |
| Tier classification (CXL-class only) | CPU page-fault policy / OS VM / swap |
| Exact capacity accounting and reservations | Generic memory reclamation / cache eviction |
| Deterministic placement selection & explanation | Model / KV placement, Resource Broker, workload scheduling |
| Generation-bound authority and stale fencing | CXL wire protocol and memory coherency implementation |
| Versioned, integrity-checked persistence | Firmware/BIOS/ACPI/switch management, vendor management |
| REAL / DERIVED / SYNTHETIC / UNSUPPORTED separation | CXL accelerator-access claims without evidence |

Adjacent ownership remains explicit: Topology Fabric supplies generic topology facts, PCIe
Fabric supplies PCIe attachment facts, NUMA Fabric supplies NUMA-domain facts. CXL Fabric
interprets CXL-specific memory consequences but never absorbs those runtimes. Memory Expansion
Fabric, which follows CXL Fabric, owns the broader expanded/pooled-memory abstraction.

## Architecture

CXL Fabric is a vendor-neutral **core** library with two narrow, explicit backends and an
optional third:

- **CXLFabric::core** - typed identities/generations, device/region/pool model, capability
  model, locality/economics model, exact capacity accounting, deterministic admission and
  selection, reservations, authority/fencing, failover, versioned+checksummed persistence, and
  the bounded framed wire protocol.
- **CXLFabric::system** - real Windows discovery via documented SetupAPI / PnP mechanisms.
- **CXLFabric::synthetic** - deterministic constructed CXL topologies (SYNTHETIC provenance).
- **CXLFabric::unsupported** - reports the absence of a supported discovery path.

The runtime exposes the cxl-fabric CLI, the real multiprocess control plane
(cxl_fabric_coordinator and cxl_fabric_worker over bounded loopback TCP), public headers,
libraries, a CMake package, runnable examples, and benchmarks.

## Typed identity / generation model

Identity and generation are strongly typed (HostId, DeviceId, RegionId, PoolId, ReservationId,
WorkerBootId, CoordinatorEpoch, PolicyGeneration, and more). They are **not** interchangeable
integers - a DeviceId cannot be silently passed as a RegionId. Generations are checked (0 is
invalid, max().next() returns invalid rather than wrapping), so a stale generation can never
be silently re-authorized.

## Device / region / pool semantics

- **Device** - identity + generation + incarnation, hardware/vendor identity, host and
  port/switch ancestry, physical/online capacity, volatility, sharing, interleave,
  accelerator access, health, lifecycle state, capability evidence, provenance, evidence
  freshness.
- **Region** - identity + generation, member devices, interleave set, capacity, online/decode
  state, access mode, consumer scope, locality, tier, health, reservation accounting,
  provenance, freshness.
- **Pool** - identity + generation, generation-bound members, governed total, committed,
  reserved, draining, unavailable, free, admission policy, failure domains, locality
  distribution, health summary, lifecycle, provenance, freshness.

A pool is not a vector of devices: membership is generation-bound, member add/remove is
transactional, and a membership change advances PoolGeneration, fencing reservations that
were authorized under the old generation.

## Capability model

Capabilities (MEMORY_DEVICE, TYPE3_MEMORY, VOLATILE_CAPACITY, PERSISTENT_CAPACITY, HOTPLUG,
MEMORY_POOLING, INTERLEAVE, ACCELERATOR_VISIBLE_PATH, CACHE_COHERENT_ACCESS, and more) carry a
support state (SUPPORTED / UNSUPPORTED / UNKNOWN / REVALIDATION_REQUIRED), version, evidence
source, evidence generation, freshness, and constraints. UNKNOWN fails closed for hard
requirements. Capability is never inferred from PCI class alone.

## Lifecycle

Devices, regions, and pools each have an explicit, validated state machine (DISCOVERED ->
ENUMERATED -> AVAILABLE -> ONLINE -> DEGRADED -> DRAINING -> REVALIDATION_REQUIRED -> OFFLINE
-> FAILED -> REMOVED -> RETIRED). Every transition is validated; there are no silent jumps. A
device rediscovered after disappearance never inherits old generation authority; a region
rebuilt from changed membership advances generation.

## Locality and access economics

Locality classes (DIRECT_LOCAL, SAME_HOST, SAME_NUMA_DOMAIN, REMOTE_NUMA_DOMAIN, SAME_CXL_ROOT,
SAME_SWITCH, SWITCH_REMOTE, MULTI_HOP_CXL, CROSS_HOST) and economic evidence classes (MEASURED,
PLATFORM_REPORTED, CONFIGURED, DERIVED, SYNTHETIC, UNKNOWN) are modeled. Nothing is reported as
a measured benchmark unless its evidence class is MEASURED.

## Capacity accounting

Capacity accounting is exact and overflow-safe. The governed invariant is
free + reserved + committed + draining + unavailable == online, and online <= total. Negative
values, integer wraparound, double release, duplicate reservation, stale-generation release,
old-epoch commit, reservation-after-drain, commit-beyond-reserved, member double-counting, and
free > total are all rejected. CapacityLine::release is atomic: a failed release never
partially mutates accounting.

## Reservations and admission

evaluate, reserve, commit, and release are distinct. A policy decision is not a reservation; a
reservation is not committed use. A hard-constraint check runs before any ranking; only
eligible candidates rank. Late/stale commits and releases are fenced by generation, epoch, and
worker boot identity.

## Deterministic selection policy

Hard constraints (current device/region/pool, ONLINE, current evidence, required capability,
sufficient capacity, volatility/persistence, host/consumer compatibility, sharing/isolation,
required locality, accelerator-access requirement, acceptable health, not draining, not failed,
policy permit) are applied first; then candidates rank deterministically by locality, latency,
bandwidth, capacity, failure-domain diversity, persistence, accelerator proximity,
interleave quality, and utilization, with a stable-identity tie-break. Every result is
structurally explained (REJECTED with explicit reason codes, or SELECTED with named ranking
factors). No opaque unexplained score is produced.

## Failover and failure domains

CXL Fabric governs failover of CXL-capacity knowledge and reservations. Failure-domain metadata
(device/port/switch/root-complex/host/rack) is used deterministically to prefer replacement
capacity outside a failed member's immediate failure domain; same-domain-only is returned
explicitly as a degraded fallback. Selecting a replacement target is **authority to use** that
target; moving data into it belongs to an adjacent movement runtime. CXL Fabric never claims
data replication.

## Authority and fencing

Every mutating operation validates CoordinatorEpoch, WorkerBootId, DeviceGeneration,
DeviceBootId, RegionGeneration, PoolGeneration, ConsumerGeneration, and PolicyGeneration as
applicable. Stale epoch, worker boot, device generation, region generation, pool generation,
consumer generation, reservation, or policy are rejected; a stale release never corrupts
accounting, a stale commit never consumes new-generation capacity, and a stale device event
never overwrites current state.

## Persistence and recovery

Durable structural state (stable IDs, device/region/pool definitions, policy generation,
provenance, configuration) is persisted in a versioned, checksummed, bounded, atomically
replaced format (temp + flush/close + rename-replace). Corruption, truncation, trailing
garbage, version mismatch, absurd counts, and overflow are all rejected. After a coordinator
restart the epoch advances, live worker authority clears, dynamic health becomes stale,
physical online state becomes REVALIDATION_REQUIRED unless re-observed, hotplug presence is not
trusted from disk, and reservations are fenced by absence (an old-epoch reservation commit is
rejected).

## Multiprocess semantics

cxl_fabric_coordinator and cxl_fabric_worker communicate over bounded loopback TCP frames with
magic, version, type, length, and CRC32 checksum, with strict malformed/truncated/oversized/
invalid-enum/invalid-generation/trailing-garbage rejection and overflow-safe lengths. A worker
publishes devices/capacity/regions/pools/health/capabilities/locality evidence and reservation
results; the coordinator tracks workers and fences evidence on worker death. Both worker
death/restart (new WorkerBootId, old-boot reservations fenced) and coordinator death/restart
(epoch advances, durable structural knowledge recovers to REVALIDATION_REQUIRED, old-epoch
reservations rejected, fresh authority re-established) are exercised as real OS-process proofs.

## REAL / DERIVED / SYNTHETIC / UNSUPPORTED

Every item of evidence is classified. REAL is directly observed from the OS or a documented
runtime/hardware API; DERIVED is computed from REAL facts; SYNTHETIC is a constructed test
topology/device; UNSUPPORTED is a capability that cannot be proven or is absent. CXL Fabric
never reports DERIVED as measured, SYNTHETIC as REAL, NUMA-attached DRAM as CXL, or host RAM
pooling as CXL without real CXL evidence.

## Actual local hardware findings

The SystemBackend enumerated this development machine (host **PAUL**) via documented
SetupAPI/PnP mechanisms:

- 286 present devices enumerated (REAL).
- No CXL memory device was conclusively identified; CXL capability is not inferred from PCI
  class alone.
- **Physical CXL memory hardware: UNSUPPORTED on this machine.**
- Accelerators present (REAL adjacency evidence, not CXL proof): **NVIDIA GeForce RTX 5090**
  and **AMD Radeon(TM) Graphics**. Direct accelerator-to-CXL access is not claimed without
  proof.

The system backend therefore reports physical CXL memory as UNSUPPORTED, and all semantic proof
is completed with the deterministic SyntheticBackend (SYNTHETIC provenance). A synthetic CXL
topology proves runtime semantics, never physical CXL hardware.

## Build

Windows 11 x64, MSVC (VS 2022), C++20, CMake 3.20 or newer.

    cmake -S . -B build
    cmake --build build --config Release
    ctest --test-dir build -C Release

Debug:

    cmake --build build --config Debug
    ctest --test-dir build -C Debug

Both Release and Debug build clean under /W4 /WX. No test timeouts are configured anywhere;
process kill is a test only where death/restart is itself the test.

Reusable library targets: CXLFabric::core, CXLFabric::system, CXLFabric::synthetic,
CXLFabric::unsupported.

## Install

    cmake --install build --config Release --prefix <prefix>

## find_package

    find_package(CXLFabric CONFIG REQUIRED)
    target_link_libraries(myapp PRIVATE CXLFabric::core)

CXLFabricConfig.cmake and CXLFabricTargets.cmake are installed under
<prefix>/lib/cmake/CXLFabric with exported targets CXLFabric::core, CXLFabric::system,
CXLFabric::synthetic, and CXLFabric::unsupported. No source-tree path leaks into the installed
package.

## CLI

    cxl-fabric discover [--backend system|synthetic|unsupported]
    cxl-fabric devices
    cxl-fabric regions
    cxl-fabric pools
    cxl-fabric capacity
    cxl-fabric explain --bytes 4G --consumer gpu0
    cxl-fabric validate-state
    cxl-fabric synthetic-demo

Output marks evidence as REAL / DERIVED / SYNTHETIC / UNSUPPORTED.

## Examples

Runnable examples: basic_discovery, capacity_accounting, pool_selection, locality_policy,
reservation_lifecycle, stale_generation, failover_selection, persistence_recovery, and
synthetic_cxl_pool. They never fabricate hardware.

## Tests

Coverage includes: IDs/generations; serialization; lifecycle; errors; enums; system, synthetic
and unsupported discovery; duplicate/conflicting device detection; capabilities (supported,
unsupported, unknown, stale); regions; pools; capacity accounting (reserve/commit/release/
exhaustion/overflow/exact closure/stale ops); policy (hard constraints, ranking, tie-break,
locality, failure-domain preference, persistence, accelerator-access, deterministic
explanation); authority (stale epoch/worker/device/region/pool/reservation/policy);
persistence (round-trip, restart, corruption, truncation, version, oversized, trailing);
protocol (valid, malformed, oversized, truncated, invalid enum/id/generation); concurrency
(reservation, failure/commit, drain/admission, membership/select, worker-death/late-result
races); a seeded randomized property test (seed printed on failure); and a real multiprocess
worker-death and coordinator-restart proof.

## Benchmarks

Benchmarks measure only legitimate CPU/runtime operations (snapshot lookup, selection,
reservation create/commit/release, serialization) and report ops/sec. No synthetic number is
reported as hardware CXL bandwidth or latency.

## Limitations

- No physical CXL memory hardware is present on this machine, so all CXL semantics are proven
  through the SyntheticBackend; no physical CXL bandwidth or latency is claimed.
- The Linux (sysfs) backend, CXL switch firmware management, and vendor-specific backends are
  intentionally out of scope for this release; a future Linux backend can be designed cleanly
  without implementing fake support.
- MEASURED evidence is never produced by this runtime; it must be supplied by an external,
  trusted measurement source.
- A synthetic topology proves runtime semantics, not physical CXL hardware.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
