#pragma once
#include <string>
#include <string_view>

namespace gusto::client::rof2 {
// Server challenges use a bracketed hex nonce. Do not answer partial tokens.
inline std::string InstanceGateCommand(std::string_view text) {
  constexpr std::string_view marker = "[GUSTO_INSTANCE_GATE:";
  const auto start = text.find(marker);
  if (start == text.npos) return {};
  const auto token = text.substr(start + marker.size());
  const auto end = token.find(']');
  if (end == token.npos || end == 0 || end > 64) return {};
  for (char c : token.substr(0, end))
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) return {};
  return "/say #gusto verify " + std::string(token.substr(0, end));
}
}
