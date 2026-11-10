#include "gusto/client/rof2/life_fx.hpp"
#include "gusto/client/rof2/life_art.hpp"
#include <algorithm>
#include <cmath>
#include <windows.h>
namespace gusto::client::rof2 {
namespace {
SRWLOCK sceneLock=SRWLOCK_INIT;
struct Guard {Guard(){AcquireSRWLockExclusive(&sceneLock);}~Guard(){ReleaseSRWLockExclusive(&sceneLock);}};
LifeOptions options;
life::Tracker tracker;
std::vector<life::Event> events;
std::uint64_t lastLaugh=0;
bool Enabled(life::Kind kind) {
  return kind==life::Kind::NpcDeath?options.npc:kind==life::Kind::PlayerDeath?options.player:options.feign;
}
std::uint32_t Tint(std::uint32_t color,std::uint32_t tint,float opacity) {
  std::uint32_t result=std::uint32_t(opacity*255)<<24;
  for(int shift:{0,8,16}) result|=((((color>>shift)&255)*((tint>>shift)&255))/255)<<shift;
  return result;
}
}
void SetLifeOptions(LifeOptions value) {
  Guard guard;
  auto clamp=[](float f,float fallback,float lo,float hi){return std::isfinite(f)?std::clamp(f,lo,hi):fallback;};
  value.duration=clamp(value.duration,8,4,20);value.rise=clamp(value.rise,1.1f,.25f,3);
  value.npcScale=clamp(value.npcScale,1.5f,.5f,4);value.playerScale=clamp(value.playerScale,2.8f,1,6);
  value.jesterScale=clamp(value.jesterScale,2,.5f,5);value.range=clamp(value.range,250,25,1000);
  value.npcColor|=0xff000000u;value.playerColor|=0xff000000u;
  options=value;std::erase_if(events,[](const auto& event){return !Enabled(event.kind);});
}
LifeOptions GetLifeOptions(){Guard guard;return options;}
bool ObserveLifeSample(const life::Sample& sample,std::uint64_t now,bool nearby) {
  Guard guard;
  auto event=tracker.Observe(sample,now);
  if (!event || !nearby || !Enabled(event->kind)) return false;
  if (events.size()>=32) events.erase(events.begin());
  events.push_back(*event);
  if (event->kind!=life::Kind::Feign || !options.laugh ||
      (lastLaugh && now>=lastLaugh && now-lastLaugh<500)) return false;
  lastLaugh=now;return true;
}
void ForgetLifeSpawn(std::uint32_t id){Guard guard;tracker.Forget(id);}
void ResetLifeEffects(){Guard guard;tracker.Clear();events.clear();lastLaugh=0;}
std::size_t LifeEffectCount(){Guard guard;return events.size();}
HRESULT DrawLifeEffects(IDirect3DDevice9* device,const D3DMATRIX& vp,hud::Point camera,std::uint64_t now) {
  std::vector<life::Event> active;LifeOptions config;
  {
    Guard guard;config=options;
    std::erase_if(events,[&](const auto& e){return now>=e.born && now-e.born>=std::uint64_t(config.duration*1000);});
    active=events;
  }
  if(active.empty())return S_FALSE;
  if(!device)return E_POINTER;
  for(const auto& row:vp.m)for(float f:row)if(!std::isfinite(f))return E_INVALIDARG;
  IDirect3DStateBlock9* saved=nullptr;
  auto hr=device->CreateStateBlock(D3DSBT_ALL,&saved);if(FAILED(hr))return hr;
  hr=saved->Capture();if(FAILED(hr)){saved->Release();return hr;}
  D3DMATRIX world{},view{},projection{},identity{};
  const bool transforms=SUCCEEDED(device->GetTransform(D3DTS_WORLD,&world)) &&
    SUCCEEDED(device->GetTransform(D3DTS_VIEW,&view)) && SUCCEEDED(device->GetTransform(D3DTS_PROJECTION,&projection));
  for(int i=0;i<4;++i)identity.m[i][i]=1;
  device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);
  device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);
  device->SetStreamSourceFreq(0,1);device->SetStreamSourceFreq(1,1);
  device->SetTransform(D3DTS_WORLD,&identity);device->SetTransform(D3DTS_VIEW,&identity);device->SetTransform(D3DTS_PROJECTION,&vp);
  device->SetTexture(0,nullptr);
  device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
  device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
  device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
  device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_FOGENABLE,FALSE);
  device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
  device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
  device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
  device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
  device->SetRenderState(D3DRS_ZENABLE,TRUE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
  device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);device->SetRenderState(D3DRS_STENCILENABLE,FALSE);
  device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
  device->SetRenderState(D3DRS_VERTEXBLEND,D3DVBF_DISABLE);device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE,FALSE);
  device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);device->SetRenderState(D3DRS_DEPTHBIAS,0);
  device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);
  std::vector<life_art::Vertex> vertices;
  for(const auto& event:active) {
    const float opacity=life::Opacity(event.born,now,config.duration);if(opacity<=0)continue;
    const bool jester=event.kind==life::Kind::Feign;
    const float scale=jester?config.jesterScale:event.kind==life::Kind::PlayerDeath?config.playerScale:config.npcScale;
    const auto tint=jester?0xffffffffu:event.kind==life::Kind::PlayerDeath?config.playerColor:config.npcColor;
    float dx=camera.x-event.position.x,dy=camera.y-event.position.y,length=std::hypot(dx,dy);
    if(!std::isfinite(length)||length<.01f){dx=0;dy=-1;length=1;}
    const float frontX=dx/length,frontY=dy/length,rightX=-frontY,rightY=frontX;
    const float sway=std::sin(float(now-event.born)/1000*.65f)*.16f,cs=std::cos(sway),sn=std::sin(sway);
    const float base=event.position.z+std::max(1.5f,event.height*.35f)+life::Rise(event.born,now,config.rise);
    auto fill=[&](const auto& mesh) {
      vertices.clear();vertices.reserve(mesh.size());
      for(const auto& v:mesh){float x=(v.x*cs-v.y*sn)*scale,y=(v.x*sn+v.y*cs)*scale;
        vertices.push_back({event.position.x+rightX*x-frontX*y,event.position.y+rightY*x-frontY*y,base+v.z*scale,Tint(v.color,tint,opacity)});}
    };
    if(jester)fill(life_art::Jester);else fill(life_art::Ghost);
    hr=device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(vertices.size()/3),vertices.data(),sizeof(life_art::Vertex));
    if(FAILED(hr))break;
  }
  saved->Apply();
  if(transforms){device->SetTransform(D3DTS_WORLD,&world);device->SetTransform(D3DTS_VIEW,&view);device->SetTransform(D3DTS_PROJECTION,&projection);}
  saved->Release();return hr;
}
}
