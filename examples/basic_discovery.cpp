
#include "cxl_fabric/backend/backend.hpp"
#include "cxl_fabric/backend/synthetic.hpp"
#include "cxl_fabric/core/fabric.hpp"
#include <iostream>

int main() {
  using namespace cxl_fabric;
  auto sys = create_system_backend();
  DiscoveryResult sr = sys->discover();
  std::cout << "system backend: cxl_memory_supported=" << (sr.cxl_memory_supported ? "true" : "false")
            << " host=" << sr.host_id.str() << "\n";
  for (const auto& n : sr.notes) std::cout << "  " << n << "\n";
  auto syn = create_synthetic_backend();
  DiscoveryResult yr = syn->discover();
  std::cout << "synthetic backend: devices=" << yr.devices.size() << " provenance=" << to_string(yr.provenance) << "\n";
  if (yr.devices.empty()) { std::cout << "FAIL: synthetic backend produced no devices\n"; return 1; }
  std::cout << "PASS: basic_discovery\n";
  return 0;
}
