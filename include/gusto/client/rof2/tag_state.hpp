#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
namespace gusto::client::rof2 {
inline constexpr std::string_view TagWireHeader = "GUSTOTAG";
inline constexpr std::string_view TagWireAbbreviatedHeader = "GT";
inline constexpr std::string_view TagChannelAnnouncement = "ChatChannel: ";
inline constexpr std::uint32_t TagOff = 0, TagNameplate = 1;
inline constexpr std::uint32_t TagPaw = 0xff20c020u, TagStop = 0xfff00000u;
struct TagMessage {
  std::string text, name;
  std::uint32_t spawnId = 0;
};
std::optional<TagMessage> ParseTagMessage(std::string_view message);
std::optional<std::string> EncodeTagMessage(const TagMessage &message);
std::string NormalizeTagName(std::string_view name);
struct TagEntry {
  std::string name, text;
  std::uint32_t color = TagOff;
};
class TagState {
public:
  bool enabled = true;
  bool defaultArrow = true;
  bool Apply(const TagMessage &message, bool npc);
  const TagEntry *Find(std::uint32_t id, std::string_view name) const;
  void Clear(std::uint32_t id = 0);
  const auto &Entries() const { return entries; }

private:
  std::unordered_map<std::uint32_t, TagEntry> entries;
};
} // namespace gusto::client::rof2
