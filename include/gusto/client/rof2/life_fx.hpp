#pragma once
#include "gusto/client/rof2/life_fx_model.hpp"
#include <d3d9.h>
namespace gusto::client::rof2 {
struct LifeOptions {
  bool npc=true,player=true,feign=true,laugh=true;
  std::uint32_t npcColor=0xffffffffu,playerColor=0xffff8024u;
  float duration=8,rise=1.1f,npcScale=1.5f,playerScale=2.8f,jesterScale=2,range=250;
};
void SetLifeOptions(LifeOptions options);
LifeOptions GetLifeOptions();
// Returns true only when this new visible FD event should play its one-shot laugh.
bool ObserveLifeSample(const life::Sample& sample,std::uint64_t now,bool nearby);
void ForgetLifeSpawn(std::uint32_t id);
void ResetLifeEffects();
std::size_t LifeEffectCount();
HRESULT DrawLifeEffects(IDirect3DDevice9* device,const D3DMATRIX& viewProjection,
                       hud::Point camera,std::uint64_t now);
}
