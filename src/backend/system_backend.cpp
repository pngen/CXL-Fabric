// ============================================================================
// CXL Fabric - SystemBackend (Windows PnP/SetupAPI real discovery)
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// ============================================================================
#include "cxl_fabric/backend/backend.hpp"

#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <cstring>
#include <string>
#include <vector>

namespace cxl_fabric {

namespace {
std::string to_utf8(const std::wstring& w) {
  if (w.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}
std::wstring host_name() { wchar_t buf[256]; DWORD sz = 256; GetComputerNameW(buf, &sz); return std::wstring(buf, sz); }
}  // namespace

class SystemBackend final : public IDiscoveryBackend {
public:
  std::string name() const override { return "system"; }
  DiscoveryResult discover() override {
    DiscoveryResult r;
    r.backend_name = name();
    r.provenance = Provenance::UNSUPPORTED;   // physical CXL memory is not proven here
    r.cxl_memory_supported = false;
    r.host_id = HostId(to_utf8(host_name()));

    HDEVINFO hdi = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hdi == INVALID_HANDLE_VALUE) {
      r.add_note("SystemBackend: SetupDiGetClassDevsW(all classes) failed; no device enumeration possible.");
      return r;
    }
    std::size_t pci_count = 0;
    std::size_t display_count = 0;
    std::size_t memory_like_count = 0;
    std::vector<std::string> display_names;
    for (DWORD idx = 0;; ++idx) {
      SP_DEVINFO_DATA did; std::memset(&did, 0, sizeof(did)); did.cbSize = sizeof(did);
      if (!SetupDiEnumDeviceInfo(hdi, idx, &did)) break;
      ++pci_count;
      wchar_t cls[256]; DWORD clsSize = sizeof(cls);
      wchar_t desc[512]; DWORD descSize = sizeof(desc);
      std::wstring wcls; std::wstring wdesc;
      if (SetupDiGetDeviceRegistryPropertyW(hdi, &did, SPDRP_CLASS, nullptr, reinterpret_cast<PBYTE>(cls), clsSize, nullptr))
        wcls = cls;
      if (SetupDiGetDeviceRegistryPropertyW(hdi, &did, SPDRP_DEVICEDESC, nullptr, reinterpret_cast<PBYTE>(desc), descSize, nullptr))
        wdesc = desc;
      // Adjacent accelerator evidence (REAL): display-class devices.
      if (wcls.find(L"Display") != std::wstring::npos) {
        ++display_count;
        display_names.push_back(to_utf8(wdesc.empty() ? wcls : wdesc));
      }
      // Memory-controller-like classes are noted but never treated as CXL:
      // capability is not inferred from PCI class alone.
      if (wcls.find(L"Memory") != std::wstring::npos || wcls.find(L"System") != std::wstring::npos)
        ++memory_like_count;
    }
    SetupDiDestroyDeviceInfoList(hdi);
    r.add_note("SystemBackend: enumerated " + std::to_string(pci_count) + " present devices via SetupAPI (REAL).");
    r.add_note("SystemBackend: no CXL memory device was conclusively identified; CXL capability is not inferred from PCI class alone.");
    r.add_note("Physical CXL memory hardware: UNSUPPORTED on this machine.");
    r.accelerators = display_names;
    r.add_note("Accelerators present (REAL adjacency evidence, not CXL proof): " + std::to_string(display_count));
    return r;
  }
};

std::unique_ptr<IDiscoveryBackend> create_system_backend() {
  return std::make_unique<SystemBackend>();
}

}  // namespace cxl_fabric