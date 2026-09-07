
#include "cxl_fabric/core/fabric.hpp"
#include "cxl_fabric/persistence/persistence.hpp"
#include "cxl_fabric/backend/synthetic.hpp"
#include <iostream>
#include <filesystem>
int main(){
  using namespace cxl_fabric;
  Fabric f(CoordinatorEpoch(1), PolicyGeneration(1));
  AuthorityContext ctx; ctx.coordinator_epoch=f.epoch(); ctx.policy_generation=f.policy_generation(); ctx.consumer_generation=ConsumerGeneration(1);
  HostId host("host-pr");
  CxlDevice d = synthetic::make_device(DeviceId("d1"), DeviceGeneration(1), DeviceBootId("b1"), host,
      16ull*1024*1024*1024, VolatilityKind::VOLATILE, SharingMode::DEDICATED, LocalityClass::SAME_HOST, 200,16000,1,"fd");
  f.register_device(d, ctx);
  f.create_pool(synthetic::make_pool(PoolId("p-1"), PoolGeneration(1),
      {{DeviceId("d1"), DeviceGeneration(1), 16ull*1024*1024*1024, true,true,"fd",LocalityClass::SAME_HOST}}), ctx);
  PersistedState st;
  if (!f.export_persisted(st)) { std::cout << "FAIL: export\n"; return 1; }
  std::vector<std::uint8_t> payload; std::string err;
  if (!serialize_state(st, payload, err)) { std::cout << "FAIL: serialize " << err << "\n"; return 1; }
  PersistedState st2;
  if (!deserialize_state(payload, st2, err)) { std::cout << "FAIL: deserialize " << err << "\n"; return 1; }
  if (st2.pools.size() != 1 || st2.devices.size() != 1) { std::cout << "FAIL: round-trip counts\n"; return 1; }
  // Corrupt payload -> must reject.
  auto bad = payload; bad[bad.size()/2] ^= 0xFF;
  PersistedState st3;
  if (deserialize_state(bad, st3, err)) { std::cout << "FAIL: corrupted payload accepted\n"; return 1; }
  std::cout << "PASS: persistence_recovery\n";
  return 0;
}
