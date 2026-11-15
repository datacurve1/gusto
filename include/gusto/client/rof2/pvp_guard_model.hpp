#pragma once
#include <string>
#include <string_view>
namespace gusto::client::rof2 {
inline std::string_view PvpTrim(std::string_view s) {
  while (!s.empty() && (s.front()==' ' || s.front()=='\t' || s.front()=='\r' || s.front()=='\n')) s.remove_prefix(1);
  while (!s.empty() && (s.back()==' ' || s.back()=='\t' || s.back()=='\r' || s.back()=='\n')) s.remove_suffix(1);
  return s;
}
inline bool IsPvpZoneName(std::string_view name) {
  std::string compact;
  for (unsigned char c : name) {
    if (c==' ' || c=='\t' || c=='\r' || c=='\n') continue;
    if (c>='A' && c<='Z') c += 'a'-'A';
    compact += char(c);
  }
  return compact.find("(pvp)") != compact.npos || compact.find("[pvp]") != compact.npos;
}
inline bool ShouldBlockPvpOff(std::string_view text, bool protectedZone, bool pvpEnabled) {
  if (!protectedZone) return false;
  std::string lower;
  for (unsigned char c : text) lower += char(c>='A' && c<='Z' ? c+'a'-'A' : c);
  auto rest = PvpTrim(lower);
  auto word = [&]() {
    const auto split = rest.find_first_of(" \t\r\n");
    const auto token = rest.substr(0,split);
    rest = split == rest.npos ? std::string_view{} : PvpTrim(rest.substr(split));
    return token;
  };
  auto cmd = word();
  if (cmd=="/say" || cmd=="/s") cmd = word();
  if (cmd=="#set") { if (word()!="pvp") return false; }
  else if (cmd!="#pvp" && cmd!="/pvp" && cmd!="#setpvp") return false;
  const auto mode = word();
  if (mode=="on" || mode=="1" || mode=="true" || mode=="enable" || mode=="enabled" || mode=="yes") return false;
  if (mode=="off" || mode=="0" || mode=="false" || mode=="disable" || mode=="disabled" || mode=="no") return true;
  // No argument toggles on this server. Never submit a potentially disabling toggle.
  return pvpEnabled;
}
}
