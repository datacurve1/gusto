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
  check(InstanceGateCommand("GUSTO_INSTANCE_GATE:abc123") == "/say #gusto verify abc123");
  check(InstanceGateCommand("prefixGUSTO_INSTANCE_GATE:abc123").empty());
  check(InstanceGateCommand("GUSTO_INSTANCE_GATE:abcxyz").empty());
  check(InstanceGateCommand("[GUSTO_INSTANCE_GATE:]").empty());
  check(InstanceGateCommand("[GUSTO_INSTANCE_GATE:abc").empty());
  check(InstanceGateCommand("[GUSTO_INSTANCE_GATE:abcxyz]").empty());
  check(InstanceGateCommand("[GUSTO_INSTANCE_GATE:" + std::string(65, 'a') + "]").empty());
  check(InstanceGateCommand("[GUSTO_INSTANCE_GATE:abc /quit]").empty());
  // Captured at the live RoF2 dsp_chat entry, before log/UI link stripping.
  const std::string metadata = "0FFFFF01CC2000000000000000000000000000000000000000000000";
  const std::string prefix = "GUSTO.asi is required for this guild or PvP instance. If installed, it will verify automatically; after the confirmation, click the book again. Verification code: [";
  const auto linked = prefix + "\x12" + metadata + "GUSTO_INSTANCE_GATE:8d6af9918d70ba3bd8\x12]";
  check(InstanceGateCommand(linked) == "/say #gusto verify 8d6af9918d70ba3bd8");
  check(InstanceGateCommand("\x12" + metadata + "GUSTO_INSTANCE_GATE:abc123\x12") == "/say #gusto verify abc123");
  check(InstanceGateCommand(prefix + "\x12" + metadata + "GUSTO_INSTANCE_GATE:abc123]").empty());
  check(InstanceGateCommand("\x12" + metadata.substr(1) + "GUSTO_INSTANCE_GATE:abc123\x12").empty());
  check(InstanceGateCommand("\x12" + std::string(56, 'z') + "GUSTO_INSTANCE_GATE:abc123\x12").empty());
  check(InstanceGateCommand("\x12" + metadata + "GUSTO_INSTANCE_GATE:abcxyz\x12").empty());
  std::printf("PASS: %d instance gate challenge checks\n", count);
}
