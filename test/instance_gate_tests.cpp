#include "gusto/client/rof2/instance_gate.hpp"
#include <cstdio>
#include <cstdlib>
using gusto::client::rof2::InstanceGateCommand;
int main() {
  int count = 0;
  auto check = [&](bool value) { if (!value) std::exit(1); ++count; };
  check(InstanceGateCommand("Verification code: [GUSTO_INSTANCE_GATE:8d6af98b0272a8ebaf]") == "/say #gusto verify 8d6af98b0272a8ebaf");
  check(InstanceGateCommand("[GUSTO_INSTANCE_GATE:ABC123]") == "/say #gusto verify ABC123");
  check(InstanceGateCommand("ordinary chat").empty());
  check(InstanceGateCommand("GUSTO_INSTANCE_GATE:abc123").empty());
  check(InstanceGateCommand("[GUSTO_INSTANCE_GATE:]").empty());
  check(InstanceGateCommand("[GUSTO_INSTANCE_GATE:abc").empty());
  check(InstanceGateCommand("[GUSTO_INSTANCE_GATE:abcxyz]").empty());
  check(InstanceGateCommand("[GUSTO_INSTANCE_GATE:" + std::string(65, 'a') + "]").empty());
  check(InstanceGateCommand("[GUSTO_INSTANCE_GATE:abc /quit]").empty());
  std::printf("PASS: %d instance gate challenge checks\n", count);
}
