// ============================================================================
// CXL Fabric - SyntheticBackend
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#include "cxl_fabric/backend/backend.hpp"
#include "cxl_fabric/backend/synthetic.hpp"

namespace cxl_fabric {

class SyntheticBackend final : public IDiscoveryBackend {
public:
  std::string name() const override { return "synthetic"; }
  DiscoveryResult discover() override {
    DiscoveryResult r;
    r.backend_name = name();
    r.provenance = Provenance::SYNTHETIC;
    r.cxl_memory_supported = true;
    r.host_id = HostId("host-synth");
    r.add_note("SyntheticBackend: deterministic constructed topology. SYNTHETIC evidence only; "
               "this never models physical CXL hardware.");
    r.devices = synthetic::baseline_devices(r.host_id);
    return r;
  }
};

std::unique_ptr<IDiscoveryBackend> create_synthetic_backend() {
  return std::make_unique<SyntheticBackend>();
}

}  // namespace cxl_fabric
