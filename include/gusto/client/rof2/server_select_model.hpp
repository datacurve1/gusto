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
inline std::string_view VanguardDisplayName(std::string_view name) {
  if (!IsVanguardServer(name)) return name;
  const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  std::size_t end = 0;
  int depth = 0;
  bool sawBracket = false, sawTag = false;
  for (std::size_t i = 0; i < name.size(); ++i) {
    const char c = name[i];
    if (c == '[') { ++depth; sawBracket = true; }
    else if (c == ']') { if (depth > 0) --depth; }
    else if ((c == 'R' || c == 'r') && depth > 0 &&
             (i + 1 == name.size() || space(name[i + 1]) || name[i + 1] == ']')) sawTag = true;
    else if (!space(c)) break;
    // Keep unrelated later tags intact, including their opening brackets.
    if (sawBracket && sawTag && depth == 0) end = i + 1;
  }
  return end && IsVanguardServer(name.substr(end)) ? name.substr(end) : name;
}
// ReadServerData's wire record: address\0, flags/u32, ID/u32, name\0, ...
inline std::string_view ServerSelectWireName(std::span<const std::uint8_t> wire) {
  if (wire.size() > 4096) return {};
  auto zero = std::find(wire.begin(), wire.end(), 0);
  if (zero == wire.end()) return {};
  auto offset = std::size_t(zero - wire.begin()) + 1;
  if (wire.size() - offset < 9) return {};
  offset += 8;
  zero = std::find(wire.begin() + offset, wire.end(), 0);
  if (zero == wire.end() || zero - (wire.begin() + offset) > 512) return {};
  return {reinterpret_cast<const char*>(wire.data() + offset), std::size_t(zero - (wire.begin() + offset))};
}
// EQClientServerData::Flags (+0x30) controls the frontend's category.
inline bool PromoteVanguardServer(std::span<const std::uint8_t> wire, std::span<std::uint8_t> record) {
  if (record.size() < 0x48 || !IsVanguardServer(ServerSelectWireName(wire))) return false;
  std::uint32_t flags = 0;
  std::memcpy(&flags, record.data() + 0x30, 4);
  // AddServer checks Preferred before Legends, so clear Preferred explicitly.
  flags = (flags & ~8u) | 16u;
  std::memcpy(record.data() + 0x30, &flags, 4);
  return true;
}
}
