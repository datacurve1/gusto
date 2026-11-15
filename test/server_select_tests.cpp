#include "gusto/client/rof2/server_select_model.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace gusto::client::rof2;
int main() {
  int count = 0;
  auto check = [&](bool ok) { ++count; if (!ok) { std::printf("FAIL server-select check %d\n", count); std::exit(1); } };
  for (auto name : {"Vanguard", "vAnGuArD", "The Vanguard [Quarm]", "V a n g u a r d", "Van-guard", "Vangaurd", "Vangurd", "Vanguarrd", "Vanguart", "Vanguardian"}) check(IsVanguardServer(name));
  for (auto name : {"", "Project Quarm", "EverQuest"}) check(!IsVanguardServer(name));
  check(!IsVanguardServer("Vanilla Guard"));
  check(!IsVanguardServer("Vanguzzd"));
  auto packet = [](const char* name) {
    std::vector<std::uint8_t> v;
    auto str = [&](const char* s) { while (*s) v.push_back(*s++); v.push_back(0); };
    auto num = [&](unsigned n) { for (int i=0;i<4;++i) v.push_back((n>>(i*8))&255); };
    str("world.example"); num(8); num(1234); str(name); str("US"); str("EN"); num(4); num(123);
    return v;
  };
  std::array<std::uint8_t,0x48> record; record.fill(0xa5);
  auto before = record;
  check(PromoteVanguardServer(packet("The VANGUARD server"), record));
  unsigned flags = 0; std::memcpy(&flags, record.data()+0x30,4);
  check((flags&16) && !(flags&8));
  std::memcpy(before.data()+0x30,&flags,4);
  check(before == record); // IDs, names, address, status and population unchanged.
  auto promoted = record;
  check(PromoteVanguardServer(packet("Vanguard"), record) && record == promoted);
  check(!PromoteVanguardServer(packet("Other Server"), record) && record == promoted);
  auto wire = packet("Vanguard");
  check(!PromoteVanguardServer({}, record));
  check(!PromoteVanguardServer(std::span(wire).first(5), record));
  check(!PromoteVanguardServer(wire, std::span(record).first(0x30)));
  std::vector<std::uint8_t> bad(64, 'x');
  check(!PromoteVanguardServer(bad, record));
  check(record == promoted);
  std::printf("PASS: %d server-select matching, category, identity and malformed-record checks\n",count);
}
