#pragma once
#include <string>
#include <string_view>

namespace gusto::client::rof2 {
// Decode RoF2 links before parsing the displayed marker.
inline std::string InstanceGateCommand(std::string_view text) {
  const auto hex = [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
  };
  std::string visible;
  if (text.find('\x12') != text.npos) {
    // RoF2 say/item links: 0x12, 56 hex metadata bytes, label, 0x12.
    // The log contains only the label, while dsp_chat receives the whole link.
    constexpr std::size_t linkMetadata = 56;
    for (std::size_t i = 0; i < text.size();) {
      if (text[i] != '\x12') { visible += text[i++]; continue; }
      const auto close = text.find('\x12', i + 1);
      if (close == text.npos || close - i - 1 <= linkMetadata) return {};
      for (std::size_t n = 1; n <= linkMetadata; ++n)
        if (!hex(text[i + n])) return {};
      visible.append(text.substr(i + 1 + linkMetadata, close - i - 1 - linkMetadata));
      i = close + 1;
    }
    text = visible;
  }
  constexpr std::string_view marker = "GUSTO_INSTANCE_GATE:";
  const auto start = text.find(marker);
  if (start == text.npos) return {};
  const bool bracketed = start > 0 && text[start - 1] == '[';
  if (start > 0 && !bracketed && text[start - 1] != ' ' && text[start - 1] != '\t') return {};
  const auto token = text.substr(start + marker.size());
  std::size_t end = 0;
  for (char c : token) {
    if (!hex(c)) break;
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
