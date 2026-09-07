// ============================================================================
// CXL Fabric - UnsupportedBackend
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#include "cxl_fabric/backend/backend.hpp"

namespace cxl_fabric {

class UnsupportedBackend final : public IDiscoveryBackend {
public:
  std::string name() const override { return "unsupported"; }
  DiscoveryResult discover() override {
    DiscoveryResult r;
    r.backend_name = name();
    r.provenance = Provenance::UNSUPPORTED;
    r.cxl_memory_supported = false;
    r.add_note("UnsupportedBackend: no supported physical CXL discovery path is available on this platform.");
    return r;
  }
};

std::unique_ptr<IDiscoveryBackend> create_unsupported_backend() {
  return std::make_unique<UnsupportedBackend>();
}

}  // namespace cxl_fabric
