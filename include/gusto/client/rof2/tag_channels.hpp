#pragma once
#include <span>
#include <string>
#include <string_view>
namespace gusto::client::rof2 {
inline bool IsTagChannelName(std::string_view text) {
  if (text.size() <= 2 || text.size() >= 48 ||
      (text[0] != 'G' && text[0] != 'g') || (text[1] != 'T' && text[1] != 't'))
    return false;
  for (unsigned char c : text)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9')))
      return false;
  return true;
}
// Native channel numbers are one-based. Prefer the current chat connection's
// list; an inactive service may retain an obsolete list with different slots.
inline int FindTagChannelNumber(std::string_view selected,
                                std::span<const std::string> current,
                                std::span<const std::string> service) {
  if (selected.empty())
    return 0;
  auto channels = current.empty() ? service : current;
  if (channels.size() > 10)
    return 0;
  const auto lower = [](unsigned char c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
  };
  for (std::size_t i = 0; i < channels.size(); ++i) {
    auto candidate = std::string_view(channels[i]);
    if (candidate.size() != selected.size())
      continue;
    bool equal = true;
    for (std::size_t c = 0; c < selected.size(); ++c)
      if (lower(candidate[c]) != lower(selected[c])) {
        equal = false;
        break;
      }
    if (equal)
      return static_cast<int>(i + 1);
  }
  return 0;
}
} // namespace gusto::client::rof2
