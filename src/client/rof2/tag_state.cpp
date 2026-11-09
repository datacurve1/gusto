// Tag protocol and field semantics adapted from Zeal's NamePlate (MIT).
// Copyright (c) 2024 Clint. See THIRD_PARTY_NOTICES.md.
#include "gusto/client/rof2/tag_state.hpp"
#include <algorithm>
#include <charconv>
#include <vector>
namespace gusto::client::rof2 {
namespace {
constexpr std::string_view delimiter = " | ";
std::vector<std::string> Fields(std::string_view text) {
  std::vector<std::string> result;
  while (!text.empty()) {
    auto end = text.find(delimiter);
    result.emplace_back(text.substr(0, end));
    if (end == text.npos)
      break;
    text.remove_prefix(end + delimiter.size());
  }
  return result;
}
std::string Clean(std::string_view text) {
  std::string result;
  bool space = false;
  for (unsigned char c : text) {
    if (c == ' ') {
      space = !result.empty();
      continue;
    }
    if (space)
      result += ' ';
    space = false;
    result += (c >= 32 && c < 127) ? char(c) : '?';
  }
  return result;
}
std::uint32_t Color(char key) {
  switch (key >= 'A' && key <= 'Z' ? key + 'a' - 'A' : key) {
  case 'r':
    return 0xffff0000u;
  case 'o':
    return 0xffff8000u;
  case 'y':
    return 0xffffff00u;
  case 'g':
    return 0xff00ff00u;
  case 'b':
    return 0xff0000ffu;
  case 'w':
    return 0xffffffffu;
  case 'p':
    return TagPaw;
  case 's':
    return TagStop;
  default:
    return TagOff;
  }
}
void Remove(TagEntry &entry, const std::string &field) {
  auto fields = Fields(entry.text);
  std::erase(fields, field);
  entry.text.clear();
  for (const auto &f : fields) {
    if (!entry.text.empty())
      entry.text += delimiter;
    entry.text += f;
  }
  if (entry.text.empty() && entry.color == TagNameplate)
    entry.color = TagOff;
}
} // namespace
std::string NormalizeTagName(std::string_view name) {
  std::string out;
  for (unsigned char c : name) {
    if (c == '\'')
      break;
    // EQ's stripName omits spawn numbers and non-name punctuation (e.g. '#').
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
          c == ' '))
      continue;
    if (c == '_')
      c = ' ';
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
    out += char(c);
  }
  return Clean(out);
}
std::optional<TagMessage> ParseTagMessage(std::string_view message) {
  if (message.size() > 256)
    return {};
  auto fields = Fields(message);
  if (fields.size() != 4 ||
      (fields[0] != TagWireHeader && fields[0] != TagWireAbbreviatedHeader &&
       fields[0] != "ZEALTAG" && fields[0] != "ZT") ||
      fields[1].empty() ||
      fields[1].size() >
          (fields[1].starts_with(TagChannelAnnouncement) ? 60u : 32u) ||
      fields[2].empty() || fields[2].size() > 63)
    return {};
  std::uint32_t id = 0;
  auto [end, error] = std::from_chars(fields[3].data(),
                                      fields[3].data() + fields[3].size(), id);
  if (error != std::errc{} || end != fields[3].data() + fields[3].size())
    return {};
  if (!id && fields[1] != "clear" &&
      !fields[1].starts_with(TagChannelAnnouncement))
    return {};
  return TagMessage{fields[1], fields[2], id};
}
std::optional<std::string> EncodeTagMessage(const TagMessage &message) {
  std::string wire = std::string(TagWireHeader) + " | " + message.text + " | " +
                     message.name + " | " + std::to_string(message.spawnId);
  if (!ParseTagMessage(wire))
    return {};
  return wire;
}
const TagEntry *TagState::Find(std::uint32_t id, std::string_view name) const {
  if (!enabled)
    return nullptr;
  auto it = entries.find(id);
  return it != entries.end() && it->second.name == NormalizeTagName(name)
             ? &it->second
             : nullptr;
}
void TagState::Clear(std::uint32_t id) {
  if (id)
    entries.erase(id);
  else
    entries.clear();
}
bool TagState::Apply(const TagMessage &message, bool npc) {
  if (!enabled)
    return false;
  if (message.text == "clear") {
    Clear();
    return true;
  }
  if (!message.spawnId || message.text.empty() || message.text.size() > 32 ||
      message.text.find(delimiter) != message.text.npos ||
      (!entries.contains(message.spawnId) && entries.size() >= 4096))
    return false;
  auto &entry = entries[message.spawnId];
  auto name = NormalizeTagName(message.name);
  if (entry.name != name)
    entry = TagEntry{name, {}, TagOff};
  std::string text = message.text;
  const bool move = text[0] == '@' || text[0] == '!';
  const bool append = text[0] == '+' || text[0] == '@';
  const bool erase = text[0] == '-' || text[0] == '!';
  if (append || erase)
    text.erase(0, 1);
  if (erase && !move && !text.empty() && text.find('^') == text.npos &&
      text[0] != '*') {
    Remove(entry, Clean(text));
    return true;
  }
  if (erase)
    entry.text.clear();
  bool prefix = false;
  if (text.size() > 2 &&
      (text[0] == '^' || (text[0] == '*' && text[2] == ':'))) {
    entry.color = Color(text[1]);
    const bool star = text[0] == '*';
    text.erase(0, star ? 3 : 2);
    prefix = true;
  } else if (entry.color == TagOff || entry.color == TagNameplate) {
    entry.color = defaultArrow && npc && (!text.empty() || !entry.text.empty())
                      ? TagNameplate
                      : TagOff;
  }
  if (prefix || append || erase) {
    auto end = text.rfind('^');
    if (end != text.npos)
      text.erase(0, end + 1);
  }
  text = Clean(text);
  if (text.empty() || !npc)
    return true;
  if (move)
    for (auto &[id, other] : entries)
      Remove(other, text);
  if (append && !entry.text.empty()) {
    auto fields = Fields(entry.text);
    auto newField = text;
    for (auto it = fields.rbegin(); it != fields.rend(); ++it) {
      if (*it == newField)
        continue;
      if (it->size() + delimiter.size() + text.size() > 32)
        break;
      text = *it + std::string(delimiter) + text;
    }
  }
  if (text.size() > 32)
    text.erase(0, text.size() - 32);
  entry.text = text;
  return true;
}
} // namespace gusto::client::rof2
