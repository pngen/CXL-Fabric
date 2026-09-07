// ============================================================================
// CXL Fabric - CXL device record
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/capability.hpp"
#include "cxl_fabric/core/economics.hpp"
#include "cxl_fabric/core/enums.hpp"
#include "cxl_fabric/core/identity.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace cxl_fabric {

// A CXL memory device. Presence is not authority: a device record is a set of
// *observations* about capacity, not a grant to allocate that capacity. The
// Fabric ledger converts observations into governed accounting.
struct CxlDevice {
  DeviceId id;                                     // stable identity
  DeviceGeneration generation;                     // generation of this record
  DeviceBootId boot_id;                            // incarnation identity when observable
  std::string raw_identity;                        // raw hardware identity string
  std::string vendor_device_id;                    // PCI vendor/device pair
  std::string cxl_device_class;                    // e.g. Type-3
  std::string device_type;                         // memory / accelerator / ...
  HostId host;                                     // attachment host
  std::vector<PortId> ports;                       // attachment ports
  std::vector<SwitchId> switch_ancestry;           // port/switch ancestry
  std::uint64_t total_physical_bytes = 0;          // physical capacity
  std::uint64_t online_bytes = 0;                  // online capacity
  VolatilityKind volatility = VolatilityKind::UNKNOWN;
  SharingMode sharing = SharingMode::UNKNOWN;
  bool multi_host_capable = false;
  bool interleave_capable = false;
  AcceleratorAccessState accelerator_access = AcceleratorAccessState::UNKNOWN;
  HealthState health = HealthState::UNKNOWN;
  DeviceState state = DeviceState::DISCOVERED;
  Capabilities capabilities;
  AccessEconomics economics;
  Provenance provenance = Provenance::UNSUPPORTED;
  EvidenceGeneration evidence_generation;
  bool evidence_current = false;
  std::string firmware_metadata;
  std::string lifecycle_note;
};

}  // namespace cxl_fabric
