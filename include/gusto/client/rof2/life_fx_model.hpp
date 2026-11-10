#pragma once
#include "gusto/client/rof2/hud_model.hpp"
#include <optional>
#include <unordered_map>
namespace gusto::client::rof2::life {
enum class Kind { NpcDeath, PlayerDeath, Feign };
struct Sample {
  std::uint32_t id=0;
  std::uintptr_t identity=0;
  std::uint8_t type=0,stand=0x64;
  int hp=-1,maximum=0;
  hud::Point position;
  float height=0;
  bool positionValid=false;
};
struct Event {
  Kind kind;
  hud::Point position;
  float height=0;
  std::uint64_t born=0;
};
class Tracker {
  struct State { Sample sample; bool dead=false; std::uint8_t liveType=0; };
  std::unordered_map<std::uint32_t,State> states;
public:
  std::optional<Event> Observe(const Sample& sample,std::uint64_t now);
  void Forget(std::uint32_t id) {states.erase(id);}
  void Clear() {states.clear();}
};
float Opacity(std::uint64_t born,std::uint64_t now,float duration);
float Rise(std::uint64_t born,std::uint64_t now,float speed);
}
