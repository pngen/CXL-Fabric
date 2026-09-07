
#include "cxl_fabric/core/accounting.hpp"
#include <cassert>
#include <iostream>

int main() {
  using namespace cxl_fabric;
  CapacityLine line;
  line.total = 1ull << 40; line.online = 1ull << 40;
  std::string err;
  if (!line.audit(err)) { std::cout << "FAIL: audit " << err << "\n"; return 1; }
  bool r1 = line.reserve((1ull << 30) * 4);   // 4 GiB
  bool r2 = line.reserve((1ull << 30) * 2);
  bool r3 = line.commit((1ull << 30) * 4);
  bool r4 = line.release((1ull << 30) * 2);
  // 4 GiB committed, 2 GiB released -> reserved now 2 GiB, free unaffected by committed release logic
  bool over = line.reserve(1ull << 50);       // too big
  std::cout << "free=" << line.free() << " reserved=" << line.reserved << " committed=" << line.committed << "\n";
  if (!(r1 && r2 && r3 && r4) || over) { std::cout << "FAIL: accounting ops\n"; return 1; }
  if (!line.audit(err)) { std::cout << "FAIL: audit " << err << "\n"; return 1; }
  std::cout << "PASS: capacity_accounting\n";
  return 0;
}
