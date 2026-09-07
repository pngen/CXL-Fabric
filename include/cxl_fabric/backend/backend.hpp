// ============================================================================
// CXL Fabric - discovery backend interface
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#pragma once
#include "cxl_fabric/core/device.hpp"
#include "cxl_fabric/core/identity.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cxl_fabric {

struct DiscoveryResult {
  std::string backend_name;
  std::vector<CxlDevice> devices;        // CXL memory devices the backend found
  HostId host_id;                        // host identity (REAL where observable)
  std::vector<std::string> accelerators; // adjacent accelerator evidence (REAL where observable)
  std::vector<std::string> notes;        // truthful findings / limitations
  bool cxl_memory_supported = false;     // only true when real CXL memory is proven
  Provenance provenance = Provenance::UNSUPPORTED;

  void add_note(const std::string& n) { notes.push_back(n); }
};

// Vendor-neutral discovery boundary. A backend produces authoritative facts;
// it never fabricates CXL presence. System/Synthetic/Unsupported all satisfy
// this interface.
class IDiscoveryBackend {
public:
  virtual ~IDiscoveryBackend() = default;
  virtual std::string name() const = 0;
  virtual DiscoveryResult discover() = 0;
};

// Backend factories. Each returns a concrete backend; ownership transfers to
// the caller. The system backend uses documented OS mechanisms and never
// fabricates CXL presence. The synthetic backend only ever produces records
// with SYNTHETIC provenance. The unsupported backend reports the absence of a
// supported discovery path.
std::unique_ptr<IDiscoveryBackend> create_system_backend();
std::unique_ptr<IDiscoveryBackend> create_synthetic_backend();
std::unique_ptr<IDiscoveryBackend> create_unsupported_backend();

}  // namespace cxl_fabric
