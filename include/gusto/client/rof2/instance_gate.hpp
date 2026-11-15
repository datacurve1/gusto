#pragma once
#include <string>
#include <string_view>

namespace gusto::client::rof2 {
// Accept the bare wire marker and the bracketed form shown in server chat.
inline std::string InstanceGateCommand(std::string_view text) {
  constexpr std::string_view marker = "GUSTO_INSTANCE_GATE:";
  const auto start = text.find(marker);
  if (start == text.npos) return {};
  const bool bracketed = start > 0 && text[start - 1] == '[';
  if (start > 0 && !bracketed && text[start - 1] != ' ' && text[start - 1] != '\t') return {};
  const auto token = text.substr(start + marker.size());
  std::size_t end = 0;
  for (char c : token) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) break;
    ++end;
  }
  if (end == 0 || end > 64) return {};
  if (bracketed) {
    if (end == token.size() || token[end] != ']') return {};
  } else if (end < token.size() && token[end] != ' ' && token[end] != '\t' &&
             token[end] != '\r' && token[end] != '\n') return {};
  return "/say #gusto verify " + std::string(token.substr(0, end));
}
}
