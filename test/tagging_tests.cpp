#include "gusto/client/rof2/tag_channels.hpp"
#include "gusto/client/rof2/tag_models.hpp"
#include "gusto/client/rof2/tag_state.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace gusto::client::rof2;
namespace {
int checks = 0;
void Check(bool value, const char *what) {
  ++checks;
  if (!value) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}
} // namespace
int main() {
  auto message = ParseTagMessage("GUSTOTAG | ^R^Assist | a_rat | 42");
  Check(message && message->spawnId == 42 && message->text == "^R^Assist",
        "Gusto wire format");
  Check(ParseTagMessage("GT | ^P^ | Bob | 12").has_value(),
        "Gusto abbreviated header");
  for (const auto *invalid :
       {"", "ZEALTAG", "ZEALTAG |  | Rat | 2", "ZTX | x | Rat | 2",
        "ZEALTAG | x | Rat | -1", "ZEALTAG | x | Rat | 4294967296",
        "ZEALTAG | x | Rat | 1junk", "ZEALTAG | x | Rat | 0",
        "ZEALTAG | x | Rat | 2 | extra", "ZEALTAG | x | Rat | "})
    Check(!ParseTagMessage(invalid), "reject malformed wire message");
  Check(!ParseTagMessage("ZT | " + std::string(33, 'x') + " | Rat | 1"),
        "reject oversize tag");
  Check(!ParseTagMessage(std::string(1024, 'x')),
        "reject oversize wire message");
  Check(NormalizeTagName("a_rat001") == "a rat", "canonical spawn name");
  Check(NormalizeTagName("Bob's_corpse02") == "bob", "canonical corpse suffix");
  Check(NormalizeTagName("#King_Akins00") == "king akins",
        "canonical special NPC name");
  for (const auto *header : {"GUSTOTAG", "GT", "ZEALTAG", "ZT"}) {
    const auto parsed =
        ParseTagMessage(std::string(header) + " | ^R^Assist | a_rat | 42");
    Check(parsed && parsed->spawnId == 42 && parsed->text == "^R^Assist",
          "accept current and legacy incoming headers");
  }
  for (const auto *header : {"GUSTOTAGX", "GTX", "GUSTO", "gustotag"})
    Check(!ParseTagMessage(std::string(header) + " | Assist | a_rat | 42"),
          "reject incorrect wire headers");
  const auto encoded = EncodeTagMessage({"^R^Assist", "a rat", 42});
  Check(encoded && *encoded == "GUSTOTAG | ^R^Assist | a rat | 42",
        "outgoing tags use GUSTOTAG");
  Check(!EncodeTagMessage({"text | injected", "a rat", 42}),
        "encoder rejects delimiter injection");
  for (const auto *channel : {"Gt123", "gt123", "GTraid", "gT1"})
    Check(IsTagChannelName(channel), "accept Gt channels in any case");
  for (const auto *channel :
       {"Zt123", "Gt", "Gt one", "Gt123:password", "Other", ""})
    Check(!IsTagChannelName(channel), "reject old or invalid channel prefixes");
  Check(!IsTagChannelName("Gt" + std::string(46, 'a')),
        "reject oversize tag channel");
  const auto broadcast =
      EncodeTagMessage({std::string(TagChannelAnnouncement) + "Gt123", "0", 0});
  Check(broadcast && *broadcast == "GUSTOTAG | ChatChannel: Gt123 | 0 | 0",
        "channel announcements use GUSTOTAG and Gt");
  const auto parsedBroadcast =
      broadcast ? ParseTagMessage(*broadcast) : std::nullopt;
  Check(parsedBroadcast && parsedBroadcast->text == "ChatChannel: Gt123",
        "parse new channel announcement");
  const auto clearWire = EncodeTagMessage({"clear", "0", 0});
  Check(clearWire && *clearWire == "GUSTOTAG | clear | 0 | 0",
        "clear broadcasts use GUSTOTAG");
  TagState wireState;
  Check(encoded && wireState.Apply(*ParseTagMessage(*encoded), true) &&
            wireState.Find(42, "a_rat")->text == "Assist",
        "new outgoing format applies through receive parser");
  Check(clearWire && wireState.Apply(*ParseTagMessage(*clearWire), true) &&
            wireState.Entries().empty(),
        "new clear message clears receiver state");
  TagState state;
  Check(state.Apply(*message, true), "apply NPC tag");
  auto tag = state.Find(42, "A_rat001");
  Check(tag && tag->color == 0xffff0000u && tag->text == "Assist",
        "color prefix and text");
  Check(!state.Find(42, "a_bat"), "spawn ID reuse with different name");
  Check(state.Apply({"+Tank", "a rat", 42}, true), "append field");
  Check(state.Find(42, "a rat")->text == "Assist | Tank",
        "append preserves fields");
  state.Apply({"+Tank", "a rat", 42}, true);
  Check(state.Find(42, "a rat")->text == "Assist | Tank",
        "chat echo cannot duplicate field");
  state.Apply({"-Assist", "a rat", 42}, true);
  Check(state.Find(42, "a rat")->text == "Tank", "erase exact field");
  state.Apply({"-Tan", "a rat", 42}, true);
  Check(state.Find(42, "a rat")->text == "Tank",
        "erase requires complete field");
  state.Apply({"@Tank", "a bat", 43}, true);
  Check(state.Find(42, "a rat")->text.empty() &&
            state.Find(43, "a bat")->text == "Tank",
        "move field between targets");
  state.Apply({"!Assist", "a rat", 42}, true);
  Check(state.Find(42, "a rat")->text == "Assist", "move and replace");
  state.Apply({"^-^Text", "a rat", 42}, true);
  Check(state.Find(42, "a rat")->color == TagOff, "disable arrow prefix");
  state.Apply({"*S:", "a rat", 42}, true);
  Check(state.Find(42, "a rat")->color == TagStop, "legacy star stop prefix");
  state.Apply({"^P^", "Bob", 7}, false);
  Check(state.Find(7, "Bob")->color == TagPaw &&
            state.Find(7, "Bob")->text.empty(),
        "player shape without NPC text");
  state.Apply({"Healer", "Bob", 7}, false);
  Check(state.Find(7, "Bob")->text.empty(), "suppress player text");
  state.Apply({"Assist", "a beetle", 8}, true);
  Check(state.Find(8, "a beetle")->color == TagNameplate, "default NPC arrow");
  state.Apply({"-Assist", "a beetle", 8}, true);
  Check(state.Find(8, "a beetle")->color == TagOff,
        "last field clears default arrow");
  state.Apply({"^R^", "a beetle", 8}, true);
  state.Apply({"-", "a beetle", 8}, true);
  Check(state.Find(8, "a beetle")->color == 0xffff0000u,
        "explicit arrow persists until disabled");
  state.Clear(7);
  Check(!state.Find(7, "Bob") && state.Find(42, "a rat"), "targeted clear");
  state.Apply({"clear", "0", 0}, true);
  Check(state.Entries().empty(), "broadcast clear all");
  state.enabled = false;
  Check(!state.Apply(*message, true) && !state.Find(42, "a rat"),
        "disabled tags do not apply");
  state.enabled = true;
  state.defaultArrow = false;
  state.Apply({"Assist", "a rat", 42}, true);
  Check(state.Find(42, "a rat")->color == TagOff, "default arrow setting");
  state.Apply({std::string(33, 'x'), "rat", 9}, true);
  Check(!state.Find(9, "rat"), "state rejects oversized direct input");
  const std::vector<std::string> currentChannels{"General", "Gt123"};
  const std::vector<std::string> staleService{"Gt123", "Trade"};
  Check(FindTagChannelNumber("gt123", currentChannels, {}) == 2,
        "resolve joined channel on the current chat connection");
  Check(FindTagChannelNumber("GT123", currentChannels, staleService) == 2,
        "current connection determines native slot despite stale service list");
  Check(FindTagChannelNumber("Trade", currentChannels, staleService) == 0,
        "do not route through stale service slots when current list is "
        "populated");
  Check(FindTagChannelNumber("gt123", {}, staleService) == 1,
        "fall back to service when current connection has no channel list");
  Check(FindTagChannelNumber("gt12", currentChannels, {}) == 0,
        "channel matching requires complete name");
  Check(FindTagChannelNumber("gt123", {}, {}) == 0,
        "pending join has no numeric slot");
  Check(FindTagChannelNumber("", currentChannels, {}) == 0,
        "no selected channel");
  const std::vector<std::string> gapChannels{"", "Gt123"};
  Check(FindTagChannelNumber("gt123", gapChannels, {}) == 2,
        "retain native slot across unreadable channel names");
  const std::vector<std::string> excessChannels(11, "Gt123");
  Check(FindTagChannelNumber("gt123", excessChannels, {}) == 0,
        "reject invalid native channel count");
  TagModels models;
  for (auto shape : {TagModels::Shape::Arrow, TagModels::Shape::Octagon,
                     TagModels::Shape::Paw}) {
    auto mesh = models.Get(shape, 0xffff0000u);
    Check(mesh.vertices.size() == (shape == TagModels::Shape::Arrow     ? 182u
                                   : shape == TagModels::Shape::Octagon ? 16u
                                                                        : 560u),
          "preserve Zeal model vertex counts");
    Check(mesh.indices.size() > 2, "model has triangles");
    for (auto index : mesh.indices)
      Check(index >= 0 && std::size_t(index) < mesh.vertices.size(),
            "valid triangle strip index");
    for (const auto &vertex : mesh.vertices)
      Check(std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                std::isfinite(vertex.z),
            "finite geometry");
  }
  // Exercise untrusted chat-like bytes, including missing fields and
  // separators.
  unsigned random = 12345;
  for (int i = 0; i < 10000; ++i) {
    std::string fuzz;
    for (int n = i % 300; n > 0; --n) {
      random = random * 1664525u + 1013904223u;
      fuzz += char(random >> 24);
    }
    (void)ParseTagMessage(fuzz);
  }
  std::printf(
      "PASS: %d tagging and model checks; 10000 malformed-message inputs\n",
      checks);
}
