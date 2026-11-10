#include "gusto/client/rof2/life_fx_model.hpp"
#include <algorithm>
#include <cmath>
namespace gusto::client::rof2::life {
namespace {
bool Dead(const Sample& s) {
  return s.type==2 || s.stand==0x78 ||
    (s.stand!=0x73 && s.maximum>0 && s.hp==0);
}
}
std::optional<Event> Tracker::Observe(const Sample& s,std::uint64_t now) {
  if (!s.id || !s.identity || s.type>2) return {};
  const bool dead=Dead(s);
  auto found=states.find(s.id);
  if (found==states.end() || found->second.sample.identity!=s.identity) {
    if (states.size()>=8192) states.clear();
    states[s.id]={s,dead,s.type};
    return {}; // Existing corpses/feigning players are never new events.
  }
  auto& prior=found->second;
  const auto before=prior.sample;
  std::optional<Kind> kind;
  if (dead && !prior.dead && prior.liveType<=1)
    kind=prior.liveType==0?Kind::PlayerDeath:Kind::NpcDeath;
  else if (!dead && !prior.dead && s.type==0 && s.stand==0x73 && before.stand!=0x73 && (s.hp>0 || s.maximum<=0))
    kind=Kind::Feign;
  if (!dead && s.type<=1) prior.liveType=s.type;
  prior.dead=dead;prior.sample=s;
  if (!s.positionValid && before.positionValid) {
    prior.sample.position=before.position;prior.sample.positionValid=true;
  }
  if (!kind || !prior.sample.positionValid) return {};
  auto position=prior.sample.position;
  if (!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z)) return {};
  const float height=std::max(std::isfinite(before.height)?before.height:0.f,std::isfinite(s.height)?s.height:0.f);
  return Event{*kind,position,std::max(0.f,height),now};
}
float Opacity(std::uint64_t born,std::uint64_t now,float duration) {
  if (now<born || !std::isfinite(duration) || duration<=0) return 0;
  const float seconds=float(now-born)/1000;
  float a=std::clamp(std::min(seconds/.75f,(duration-seconds)/1.8f),0.f,1.f);
  return a*a*(3-2*a);
}
float Rise(std::uint64_t born,std::uint64_t now,float speed) {
  if (now<born || !std::isfinite(speed) || speed<0) return 0;
  return float(now-born)/1000*speed;
}
}
