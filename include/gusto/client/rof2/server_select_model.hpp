#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
namespace gusto::client::rof2 {
inline bool IsVanguardServer(std::string_view name) {
  constexpr std::string_view wanted = "vanguard";
  auto closeWord = [&](std::string_view word) {
    if (word == wanted) return true;
    if (word.size() + 1 < wanted.size() || word.size() > wanted.size() + 1) return false;
    std::size_t i = 0;
    while (i < word.size() && i < wanted.size() && word[i] == wanted[i]) ++i;
    if (word.size() == wanted.size()) {
      if (word.substr(i + 1) == wanted.substr(i + 1)) return true;
      return i + 1 < word.size() && word[i] == wanted[i + 1] && word[i + 1] == wanted[i] &&
             word.substr(i + 2) == wanted.substr(i + 2);
    }
    return word.size() < wanted.size() ? word.substr(i) == wanted.substr(i + 1)
                                       : word.substr(i + 1) == wanted.substr(i);
  };
  std::string compact, word;
  bool matched = false;
  for (unsigned char c : name) {
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      compact += char(c); word += char(c);
    } else {
      matched |= closeWord(word); word.clear();
    }
  }
  return matched || closeWord(word) || compact.find(wanted) != compact.npos;
}
// ReadServerData's wire record: address\0, flags/u32, ID/u32, name\0, ...
// EQClientServerData::Flags (+0x30) controls the frontend's category.
inline bool PromoteVanguardServer(std::span<const std::uint8_t> wire, std::span<std::uint8_t> record) {
  if (record.size() < 0x48 || wire.size() > 4096) return false;
  auto zero = std::find(wire.begin(), wire.end(), 0);
  if (zero == wire.end()) return false;
  auto offset = std::size_t(zero - wire.begin()) + 1;
  if (wire.size() - offset < 9) return false;
  offset += 8;
  zero = std::find(wire.begin() + offset, wire.end(), 0);
  if (zero == wire.end()) return false;
  const std::string_view name(reinterpret_cast<const char*>(wire.data() + offset), zero - (wire.begin() + offset));
  if (name.size() > 512 || !IsVanguardServer(name)) return false;
  std::uint32_t flags = 0;
  std::memcpy(&flags, record.data() + 0x30, 4);
  // AddServer checks Preferred before Legends, so clear Preferred explicitly.
  flags = (flags & ~8u) | 16u;
  std::memcpy(record.data() + 0x30, &flags, 4);
  return true;
}
}
