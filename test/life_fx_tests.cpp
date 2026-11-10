#include "gusto/client/rof2/life_fx_model.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace gusto::client::rof2;
namespace {int checks=0;void Check(bool value,const char* text){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}}
int main(){
  life::Tracker tracker;
  life::Sample s{1,123,1,0x64,100,100,{1,2,3},6,true};
  Check(!tracker.Observe(s,100),"initial live NPC has no effect");
  s.hp=0;s.stand=0x78;auto event=tracker.Observe(s,200);
  Check(event && event->kind==life::Kind::NpcDeath,"NPC death produces white ghost event");
  Check(!tracker.Observe(s,210),"dead pose does not retrigger");
  s.type=2;Check(!tracker.Observe(s,220),"corpse conversion does not duplicate death");
  s.id=2;s.identity=456;
  Check(!tracker.Observe(s,300),"existing corpse never triggers on first sight");
  s.type=0;s.stand=0x64;s.hp=100;
  Check(!tracker.Observe(s,400),"resurrection reestablishes a living player");
  s.stand=0x73;event=tracker.Observe(s,500);
  Check(event && event->kind==life::Kind::Feign,"only player feign transition produces jester");
  Check(!tracker.Observe(s,510),"remaining feigned does not repeat jester");
  s.stand=0x64;tracker.Observe(s,600);s.stand=0x73;
  Check(tracker.Observe(s,700).has_value(),"standing then feigning can trigger again");
  s.stand=0x78;s.hp=0;event=tracker.Observe(s,800);
  Check(event && event->kind==life::Kind::PlayerDeath,"death while feigned remains a player death");
  s.id=3;s.identity=789;s.type=1;s.hp=100;s.stand=0x64;tracker.Observe(s,900);
  s.stand=0x73;Check(!tracker.Observe(s,1000),"NPC feign animation does not create player jester");
  s.stand=0x78;s.hp=0;s.positionValid=false;event=tracker.Observe(s,1100);
  Check(event && event->position.x==1 && event->height==6,"last valid graphics position survives actor removal");
  tracker.Forget(3);s.type=2;s.positionValid=true;
  Check(!tracker.Observe(s,1200),"despawn and ID reuse establish a new baseline");
  s.type=0;s.hp=100;s.stand=0x64;s.identity=999;
  Check(!tracker.Observe(s,1300),"object identity reuse cannot create death");
  s.hp=0;event=tracker.Observe(s,1400);
  Check(event && event->kind==life::Kind::PlayerDeath,"known positive-to-zero HP transition detects death");
  tracker.Clear();Check(!tracker.Observe(s,1500),"zoning clears transition history");
  s.id=4;s.identity=111;s.hp=-1;s.maximum=0;s.stand=0x64;tracker.Observe(s,1600);
  Check(!tracker.Observe(s,1700),"unknown HP cannot imply death");
  s.id=5;s.identity=222;s.stand=0x73;s.hp=50;s.maximum=100;
  Check(!tracker.Observe(s,1800),"already-feigned player at zone entry is not new FD");
  s.id=6;s.identity=333;s.hp=100;s.maximum=100;s.stand=0x64;tracker.Observe(s,1900);
  s.hp=0;s.stand=0x73;Check(!tracker.Observe(s,1950),"zero known HP in a stale FD pose is not a new feign");
  s.stand=0x78;Check(tracker.Observe(s,2000).has_value(),"confirmed death after stale FD pose still emits a ghost");
  Check(life::Opacity(1000,1000,8)==0 && life::Opacity(1000,1750,8)==1,"fade in completes at 750 ms");
  Check(life::Opacity(1000,7500,8)>0 && life::Opacity(1000,9000,8)==0,"fade out completes at configured lifetime");
  Check(life::Opacity(1000,999,8)==0 && life::Opacity(1000,1100,0)==0,"invalid lifetimes and clocks are harmless");
  Check(std::fabs(life::Rise(1000,3000,1.1f)-2.2f)<.001f && life::Rise(1000,999,1)==0,"rise is slow and independent of frame rate");
  std::printf("PASS: %d life-event and animation checks\n",checks);
}
