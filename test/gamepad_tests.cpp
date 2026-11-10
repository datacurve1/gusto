#include "gusto/client/rof2/gamepad_model.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <utility>
#include <limits>
using namespace gusto::client::rof2;
int main(){
 int checks=0;auto check=[&](bool value){++checks;if(!value){std::printf("FAIL gamepad check %d\n",checks);std::exit(1);}};
 pad::Options o;o.enabled=true;o.bindings.fill(-1);o.bindings[0]=2;o.bindings[1]=2;o.bindings[2]=1;
 pad::Edges edges;pad::Inputs input{};std::vector<std::pair<int,bool>> events;
 auto step=[&](bool allowed=true){events.clear();edges.Step(input,o,allowed,[&](int id,bool down){events.emplace_back(id,down);});};
 input[0]=true;step();check(events.empty()); // must see neutral before first activation
 input={};step();check(events.empty());input[0]=true;step();check(events==decltype(events){{2,true}});
 step();check(events.empty());input[1]=true;step();check(events.empty());input[0]=false;step();check(events.empty());
 input[1]=false;step();check(events==decltype(events){{2,false}});
 input[2]=true;step();check(events==decltype(events){{1,true}});step(false);check(events==decltype(events){{1,false}});
 step();check(events.empty());input={};step();input[2]=true;step();check(events==decltype(events){{1,true}});
 o.bindings[2]=37;step();check(events==decltype(events){{1,false},{37,true}});
 o.enabled=false;step();check(events==decltype(events){{37,false}});o.enabled=true;step();check(events.empty());
 input={};step();input[0]=input[2]=true;step();check(events.size()==2);
 input={};step(false);check(events.size()==2); // disconnect releases both
 o.deadzone=std::numeric_limits<float>::quiet_NaN();o.slot=99;o.bindings[0]=999;o.bindings[1]=-90;pad::Clamp(o);
 check(o.deadzone==.25f && o.slot==15 && o.bindings[0]==-1 && o.bindings[1]==-1);
 o.bindings.fill(-1);o.bindings[0]=o.bindings[1]=pad::MouseLeft;o.bindings[2]=pad::MouseMiddle;
 edges={};input={};step();input[0]=true;step();check(events==decltype(events){{pad::MouseLeft,true}});
 input[1]=true;step();check(events.empty());input[0]=false;step();check(events.empty());step(false);check(events==decltype(events){{pad::MouseLeft,false}});
 pad::MouseMotion motion;auto delta=motion.Step(1,0,.25f,900,.02f);check(delta[0]>=17 && delta[0]<=18 && delta[1]==0);
 delta=motion.Step(.1f,.1f,.25f,900,.02f);check(delta[0]==0 && delta[1]==0);
 delta=motion.Step(0,1,.25f,900,.02f);check(delta[0]==0 && delta[1]>=-18 && delta[1]<=-17);
 delta=motion.Step(0,1,.25f,900,.02f,true);check(delta[1]>=17 && delta[1]<=18);
 delta=motion.Step(1,0,.25f,900,5);check(delta[0]>=44 && delta[0]<=45); // frame stall cannot fling pointer
 delta=motion.Step(std::numeric_limits<float>::quiet_NaN(),0,.25f,900,.02f);check(delta[0]==0 && delta[1]==0);
 o.mouseStick=50;o.mouseSpeed=std::numeric_limits<float>::infinity();pad::Clamp(o);check(o.mouseStick==2 && o.mouseSpeed==900);
 check(pad::NormalizeJoystickAxis(0,0,65535)==-1 && pad::NormalizeJoystickAxis(65535,0,65535)==1);
 check(pad::NormalizeJoystickAxis(100,100,100)==0 && pad::NormalizeJoystickAxis(100,1000,2000)==-1);
 pad::JoystickSample raw;pad::Options legacy;raw.axes={.5f,-.5f,1,-1,0,0};raw.buttons=1;raw.pov=4500;
 auto mapped=pad::MapJoystick(raw,legacy);check(mapped.buttons[0] && mapped.buttons[10] && mapped.buttons[13] && !mapped.buttons[11]);
 check(mapped.sticks[0]==.5f && mapped.sticks[1]==.5f && mapped.sticks[2]==1 && mapped.sticks[3]==1);
 legacy.joystickLayout=1;raw.axes[2]=-.8f;raw.axes[4]=.7f;mapped=pad::MapJoystick(raw,legacy);check(mapped.sticks[2]==.7f && !mapped.buttons[14] && mapped.buttons[15]);
 legacy.joystickLayout=2;raw.axes[4]=raw.axes[5]=-1;mapped=pad::MapJoystick(raw,legacy);check(!mapped.buttons[14] && !mapped.buttons[15]);
 raw.axes[5]=1;mapped=pad::MapJoystick(raw,legacy);check(!mapped.buttons[14] && mapped.buttons[15]);
 legacy.joystickButtons[0]=31;raw.buttons=0x80000000u;mapped=pad::MapJoystick(raw,legacy);check(mapped.buttons[0]);
 pad::KeyboardEdges keys;std::array<bool,pad::Actions> held{};std::vector<std::pair<int,bool>> keyEvents;
 auto keyStep=[&](){keyEvents.clear();keys.Step(held,[&](int key,bool down){keyEvents.emplace_back(key,down);});};
 held[pad::Control|'1']=true;keyStep();check(keyEvents==decltype(keyEvents){{17,true},{'1',true}});
 held[pad::Control|'2']=true;keyStep();check(keyEvents==decltype(keyEvents){{'2',true}});
 held[pad::Control|'1']=false;keyStep();check(keyEvents==decltype(keyEvents){{'1',false}} && keys.held[17]);
 held={};keyStep();check(keyEvents==decltype(keyEvents){{'2',false},{17,false}});
 held['W']=held[pad::Shift|'W']=true;keyStep();check(keys.held['W']&&keys.held[16]);held['W']=false;keyStep();check(keyEvents.empty());held={};keyStep();check(keyEvents==decltype(keyEvents){{'W',false},{16,false}});
 check(!pad::KeyBinding(1) && !pad::KeyBinding(255) && pad::KeyBinding(pad::Alt|0x70));
 pad::KeyCapture capture;capture.Begin();capture.Event(17,true);capture.Event(16,true);capture.Event('K',true);check(capture.Consume()==(pad::Control|pad::Shift|'K') && capture.draining && !capture.active);
 capture.Event('K',true);check(capture.Consume()==-1);capture.Drain(false);check(capture.draining);capture.Drain(true);check(!capture.draining);
 capture.Begin(pad::Alt);capture.Event('F',true);check(capture.Consume()==(pad::Alt|'F'));
 capture.Begin();capture.Event(27,true);check(capture.Consume()==-3 && !capture.active);capture.Drain(true);check(!capture.draining);
 for(int mouse=0;mouse<3;++mouse){
  capture.Begin();capture.Event(mouse==0?1:mouse==1?2:4,true);
  check(capture.Consume()==pad::MouseLeft+mouse && capture.draining);
  capture.Drain(true);check(capture.draining);
  capture.Event(mouse==0?1:mouse==1?2:4,false);capture.Drain(true);check(!capture.draining);
 }
 check(pad::DirectInputScan(0x27,0x4d)==0xcd && pad::DirectInputScan(0x27,0xe04d)==0xcd);
 check(pad::DirectInputScan(0x90,0xe045)==0x45 && pad::DirectInputScan(0x13,0xe11d)==0xc5);
 check(pad::KeyFromDirectInputScan(0xcd,0)==0x27 && pad::KeyFromDirectInputScan(0x45,0)==0x90);
 check(!pad::KeyboardTypingGate(1,false));check(pad::KeyboardTypingGate(2,false));check(pad::KeyboardTypingGate(0,false));check(pad::KeyboardTypingGate(1,true));check(pad::KeyboardTypingGate(0,true));
 pad::StickDirections sticks;pad::Inputs directions{};
 sticks.Step({8000,0,0,0},.25f,directions);check(!directions[17]);
 sticks.Step({10000,12000,-14000,-15000},.25f,directions);check(directions[17]&&directions[18]&&directions[20]&&directions[23]);
 for(int frame=0;frame<1800;++frame)sticks.Step({frame%2?8100:9000,12000,-14000,-15000},.25f,directions);
 check(directions[17]&&directions[18]&&directions[20]&&directions[23]);
 sticks.Step({6000,6000,-6000,-6000},.25f,directions);check(directions[17]&&directions[18]&&directions[20]&&directions[23]);
 sticks.Step({0,0,0,0},.25f,directions);check(std::none_of(directions.begin(),directions.end(),[](bool value){return value;}));
 sticks.Step({15000,0,0,0},.25f,directions);sticks.Step({-15000,0,0,0},.25f,directions);check(directions[16]&&!directions[17]);
 sticks.Reset();sticks.Step({6000,0,0,0},.25f,directions);check(!directions[16]&&!directions[17]);
 std::printf("PASS: %d gamepad checks (edges, shared mappings, focus/disconnect, neutral rearm, remap and bounds)\n",checks);
}
