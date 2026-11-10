#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace gusto::client::rof2::pad {
constexpr int Count=24, Keys=256, Shift=0x100,Control=0x200,Alt=0x400;
constexpr int MouseLeft=0x800, MouseRight=0x801, MouseMiddle=0x802, Actions=0x803;
inline bool KeyBinding(int value){
 if(value<0 || value>=MouseLeft)return false;int key=value&255;
 return key==8||key==9||key==13||(key>=16&&key<=20)||key==27||(key>=32&&key<=46)||(key>=48&&key<=57)||(key>=65&&key<=90)||(key>=96&&key<=135)||key==144||key==145||(key>=186&&key<=192)||(key>=219&&key<=222);
}
inline bool ValidBinding(int value){return value==-1 || KeyBinding(value) || (value>=MouseLeft && value<=MouseMiddle);}
// RoF2's SetChatMode(1) clears edit focus; SetChatMode(0) enters it.
// ManagerMode 20 describes any held non-modifier key, including movement.
// Only a focused editable control owns typed input; buttons retain UI focus.
inline bool KeyboardTypingGate(int chatMode,bool focusedTextInput){return chatMode!=1 || focusedTextInput;}
using Inputs=std::array<bool,Count>;
struct Options {
 bool enabled=false;
 int slot=0,backend=0,joystickLayout=0;float deadzone=.25f;
 std::array<int,16> joystickButtons={0,1,2,3,4,5,6,7,8,9,-1,-1,-1,-1,10,11};
 int mouseStick=0;float mouseSpeed=900; // 0 off, 1 left, 2 right; pixels/sec
 bool invertMouseY=false;
 std::array<int,Count> bindings={0x20,0x43,0x45,0x51,0x54,0x09,0x49,0x78,0x24,0x35,0x31,0x32,0x33,0x34,0x2d,0x2e,0x25,0x27,0x26,0x28,0x41,0x44,0x21,0x22};
};
inline void Clamp(Options& o){o.slot=std::clamp(o.slot,0,15);o.backend=std::clamp(o.backend,0,2);o.joystickLayout=std::clamp(o.joystickLayout,0,2);for(auto& button:o.joystickButtons)button=std::clamp(button,-1,31);o.deadzone=std::isfinite(o.deadzone)?std::clamp(o.deadzone,.1f,.85f):.25f;o.mouseStick=std::clamp(o.mouseStick,0,2);o.mouseSpeed=std::isfinite(o.mouseSpeed)?std::clamp(o.mouseSpeed,100.f,2400.f):900.f;for(auto& b:o.bindings)if(!ValidBinding(b))b=-1;}
// WinMM axes are device-range values; normalize before applying shared dead zones.
inline float NormalizeJoystickAxis(std::uint32_t value,std::uint32_t lo,std::uint32_t hi){
 if(hi<=lo)return 0;
 return float(std::clamp((double(value)-lo)/(double(hi)-lo)*2-1,-1.,1.));
}
struct JoystickSample {std::array<float,6> axes{};std::uint32_t buttons=0;int pov=-1;};
struct JoystickMapping {Inputs buttons{};std::array<float,4> sticks{};};
inline JoystickMapping MapJoystick(const JoystickSample& raw,const Options& cfg){
 JoystickMapping out;
 for(int i=0;i<16;++i)if(cfg.joystickButtons[i]>=0 && cfg.joystickButtons[i]<32)out.buttons[i]=(raw.buttons&(std::uint32_t(1)<<cfg.joystickButtons[i]))!=0;
 if(raw.pov>=0 && raw.pov<36000){
  const bool directions[]={raw.pov>=31500||raw.pov<=4500,raw.pov>=13500&&raw.pov<=22500,raw.pov>=22500&&raw.pov<=31500,raw.pov>=4500&&raw.pov<=13500};
  for(int i=0;i<4;++i)if(cfg.joystickButtons[10+i]<0)out.buttons[10+i]=directions[i];
 }
 out.sticks={raw.axes[0],-raw.axes[1],raw.axes[cfg.joystickLayout==1?4:2],-raw.axes[3]};
 if(cfg.joystickLayout==1){out.buttons[14]=raw.axes[2]>.2f;out.buttons[15]=raw.axes[2]<-.2f;}
 if(cfg.joystickLayout==2){out.buttons[14]=raw.axes[4]>-.6f;out.buttons[15]=raw.axes[5]>-.6f;}
 return out;
}
// Aggregate bindings before computing edges: two inputs sharing a command hold
// one native press until BOTH are released. Blocked input must return to neutral.
// Latch each stick direction until it returns toward center. A lower release
// threshold prevents jitter at the activation deadzone from interrupting a hold.
struct StickDirections {
 std::array<int,4> held{};
 void Reset(){held.fill(0);}
 void Step(const std::array<int,4>& axes,float deadzone,Inputs& input){
  const float press=deadzone*32767,release=press*.7f;
  for(int i=0;i<4;++i){
   int& direction=held[i];const int value=axes[i];
   if(direction<0 && value>=-release)direction=0;
   if(direction>0 && value<=release)direction=0;
   if(value>press)direction=1;else if(value<-press)direction=-1;
   const bool vertical=i%2;
   input[16+i*2]=vertical?direction>0:direction<0;
   input[17+i*2]=vertical?direction<0:direction>0;
  }
 }
};
struct MouseMotion {
 double x=0,y=0;
 void Reset(){x=y=0;}
 std::array<int,2> Step(float axisX,float axisY,float deadzone,float speed,float seconds,bool invert=false){
  if(!std::isfinite(axisX)||!std::isfinite(axisY)||!std::isfinite(seconds)||seconds<=0){Reset();return {};}
  float length=std::hypot(axisX,axisY);
  if(length<=deadzone){Reset();return {};}
  float amount=std::clamp((std::min(length,1.f)-deadzone)/(1-deadzone),0.f,1.f);
  float distance=amount*amount*speed*std::min(seconds,.05f);
  x+=axisX/length*distance;y+=(invert?axisY:-axisY)/length*distance;
  std::array<int,2> result={int(x),int(y)};x-=result[0];y-=result[1];return result;
 }
};
// Expand held shortcuts into a shared key set. Shared modifiers/keys release
// only when no controller binding needs them, with ordered modifier transitions.
inline unsigned DirectInputScan(unsigned vk,unsigned mapped){
 switch(vk){
 case 0x13:return 0xc5;case 0x90:return 0x45;case 0x2c:return 0xb7;case 0x6f:return 0xb5;
 case 0x21:return 0xc9;case 0x22:return 0xd1;case 0x23:return 0xcf;case 0x24:return 0xc7;
 case 0x25:return 0xcb;case 0x26:return 0xc8;case 0x27:return 0xcd;case 0x28:return 0xd0;
 case 0x2d:return 0xd2;case 0x2e:return 0xd3;
 default:return (mapped&255)|((mapped&0xff00)?0x80:0);
 }
}
inline unsigned KeyFromDirectInputScan(unsigned scan,unsigned mapped){
 switch(scan){
 case 0xc5:return 0x13;case 0x45:return 0x90;case 0xb7:return 0x2c;case 0xb5:return 0x6f;
 case 0xc9:return 0x21;case 0xd1:return 0x22;case 0xcf:return 0x23;case 0xc7:return 0x24;
 case 0xcb:return 0x25;case 0xc8:return 0x26;case 0xcd:return 0x27;case 0xd0:return 0x28;
 case 0xd2:return 0x2d;case 0xd3:return 0x2e;
 default:return mapped;
 }
}
struct KeyCapture {
 bool active=false,draining=false;int modifiers=0,result=-1;unsigned mouseHeld=0;
 void Begin(int initialModifiers=0){active=true;draining=false;modifiers=initialModifiers;result=-1;mouseHeld=0;}
 void Cancel(){active=false;draining=true;result=-3;}
 void Event(int vk,bool down){
  const int mouse=vk==1?0:vk==2?1:vk==4?2:-1;
  if(mouse>=0){
   if(active||draining){if(down)mouseHeld|=1u<<mouse;else mouseHeld&=~(1u<<mouse);}
   if(active&&down){result=MouseLeft+mouse;active=false;draining=true;}
   return;
  }
  int mask=(vk==16||vk==160||vk==161)?Shift:(vk==17||vk==162||vk==163)?Control:(vk==18||vk==164||vk==165)?Alt:0;
  if(mask){if(down)modifiers|=mask;else modifiers&=~mask;return;}
  if(!active || !down)return;
  if(vk==27){Cancel();return;}
  if(KeyBinding(vk)){result=vk|modifiers;active=false;draining=true;}
 }
 void Drain(bool neutral){if(!active && neutral && !mouseHeld)draining=false;}
 int Consume(){int value=result;result=-1;return value;}
};
struct KeyboardEdges {
 std::array<bool,Keys> held{};
 template<class Send> void Step(const std::array<bool,Actions>& actions,Send send){
  std::array<bool,Keys> next{};
  for(int action=0;action<MouseLeft;++action)if(actions[action] && KeyBinding(action)){
   next[action&255]=true;if(action&Shift)next[16]=true;if(action&Control)next[17]=true;if(action&Alt)next[18]=true;
  }
  auto modifier=[](int key){return key==16 || key==17 || key==18;};
  for(int key=0;key<Keys;++key)if(!modifier(key) && held[key]&&!next[key])send(key,false);
  for(int key:{16,17,18})if(held[key]&&!next[key])send(key,false);
  for(int key:{16,17,18})if(!held[key]&&next[key])send(key,true);
  for(int key=0;key<Keys;++key)if(!modifier(key) && !held[key]&&next[key])send(key,true);
  held=next;
 }
};
struct Edges {
 std::array<bool,Actions> held{};bool armed=false;
 template<class Send> void Step(const Inputs& input,const Options& opts,bool allowed,Send send){
  std::array<bool,Actions> next{};
  bool neutral=std::none_of(input.begin(),input.end(),[](bool b){return b;});
  if(!allowed || !opts.enabled)armed=false;
  else if(neutral)armed=true;
  if(allowed && opts.enabled && armed)for(int i=0;i<Count;++i)if(input[i] && opts.bindings[i]>=0 && opts.bindings[i]<Actions)next[opts.bindings[i]]=true;
  for(int i=0;i<Actions;++i)if(held[i]&&!next[i])send(i,false);
  for(int i=0;i<Actions;++i)if(!held[i]&&next[i])send(i,true);
  held=next;
 }
};
}
