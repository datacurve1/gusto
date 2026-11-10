#include "gusto/client/rof2/hud_model.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace gusto::client::rof2::hud;
namespace {
int checks = 0;
void Check(bool v, const char *s) {
  ++checks;
  if (!v) {
    std::fprintf(stderr, "FAIL %s\n", s);
    std::exit(1);
  }
}
bool Near(float a, float b, float e = .001f) { return std::fabs(a - b) < e; }
} // namespace
int main() {
  auto m = ParseMap("L -20, 10, 0, 20, 10, 5, 255, 128, 0\r\nP 1, 2, 3, 0, "
                    "255, 0, 2, Zone_Entrance\r\n",
                    2);
  Check(m.lines.size() == 1 && m.labels.size() == 1,
        "parse native map records and CRLF");
  Check(m.lines[0].layer == 2 && m.lines[0].color == 0xffff8000u,
        "map layers and colors");
  Check(m.labels[0].text == "Zone Entrance" && m.labels[0].size == 2,
        "labels and underscore translation");
  Check(ParseMap("L 0 0 0 1 1 1 0 0 0", -1).lines.empty(),
        "reject invalid layers");
  Check(ParseMap("L nan 0 0 1 1 1 0 0 0\nL 0 0 0 inf 0 0 1 1 1\nL 0 0\nP 1 2 3 "
                 "1 2 3 1\n",
                 0)
            .lines.empty(),
        "reject malformed nonfinite maps");
  Check(ParseMap("L 0 0 0 99999999 0 0 1 2 3\n", 0).lines.empty(),
        "bound coordinate magnitude");
  Check(ValidZoneName("poknowledge") && !ValidZoneName("../secret") &&
            !ValidZoneName("C:foo") && !ValidZoneName(""),
        "safe map file names");
  Point me{100, 200, 10};
  auto p = ProjectMap({-100, -200, 10}, me, 0, 86, 600, false);
  Check(Near(p.x, 0) && Near(p.y, 0),
        "player remains centered using native minus X/Y map coordinates");
  p = ProjectMap({-100, -800, 10}, me, 0, 86, 600, false);
  Check(Near(p.x, 0) && Near(p.y, -86), "north is up");
  p = ProjectMap({-700, -200, 10}, me, 128, 86, 600, true);
  Check(Near(p.x, 0) && Near(p.y, -86),
        "heading rotation puts player's facing up");
  Point a{-2, 0}, b{2, 0};
  Check(ClipCircle(a, b, 1) && Near(a.x, -1) && Near(b.x, 1),
        "clip crossing segment with both ends outside");
  a = {0, 0};
  b = {2, 0};
  Check(ClipCircle(a, b, 1) && Near(b.x, 1), "clip outgoing line");
  a = {2, 2};
  b = {4, 4};
  Check(!ClipCircle(a, b, 1), "reject line outside circle");
  a = {0, 0};
  b = a;
  Check(ClipCircle(a, b, 1), "degenerate point inside circle");
  a = {2, 0};
  b = a;
  Check(!ClipCircle(a, b, 1), "degenerate point outside circle");
  a = {0, 0};
  b = {1, 1};
  Check(!ClipCircle(a, b, 0), "reject invalid radius");
  Check(Near(HeightAlpha(0, 0, true, 30, 10), 1) &&
            Near(HeightAlpha(90, 0, true, 30, 10), .15f),
        "Auto Z above and floor");
  Check(Near(HeightAlpha(-30, 0, true, 30, 10), .15f) &&
            Near(HeightAlpha(1000, 0, false, 30, 30), 1),
        "asymmetric Auto Z and disabled behavior");
  auto one = Smooth(0, 100, .1f, .12f), many = 0.f;
  for (int i = 0; i < 10; ++i)
    many = Smooth(many, 100, .01f, .12f);
  Check(Near(one, many), "camera smoothing is frame-rate independent");
  Check(Near(Smooth(1, 2, .01f, 0), 2), "disabled smoothing");
  Check(SmoothAngle(511, 1, .1f, .12f) > 511,
        "camera angles cross wrap by shortest path");
  Check(Near(Smooth(1, 2, std::numeric_limits<float>::quiet_NaN(), .1f), 1),
        "invalid frame time cannot poison camera");
  FloatEffect e;
  e.born = 1000;
  Check(Near(e.Alpha(1000), 1) && Near(e.Alpha(2250), .5f) &&
            Near(e.Life(3500), 1),
        "Zeal normal damage lifetime");
  Check(Near(e.Rise(2000), 80) && Near(e.Rise(999), 0),
        "Zeal rise speed and monotonic guard");
  e.critical = true;
  Check(Near(e.Life(4000), 1) && Near(e.Alpha(4000), .4f),
        "Zeal highlighted lifetime and slower fade");
  Check(Near(ResourceRatio(50,100),.5f) && Near(ResourceRatio(0,100),0),
        "resource bar distinguishes known empty from unknown");
  Check(ResourceRatio(1,0)<0 && ResourceRatio(-1,100)<0 && Near(ResourceRatio(120,100),1),
        "hide unavailable resources and clamp overfill");
  HealthSample hp{123,40,100,false,false,false};
  auto next=hp; next.current=55;
  auto gain=DetectHealing(hp,next);
  Check(gain.amount==15 && gain.percent,"NPC healing displays percent");
  next.identity=456;
  Check(DetectHealing(hp,next).amount==0,"spawn reuse cannot create healing");
  next=hp; next.maximum=200; next.current=120;
  Check(DetectHealing(hp,next).amount==0,"max HP changes are not healing");
  next=hp; next.current=30;
  Check(DetectHealing(hp,next).amount==0,"damage cannot produce a heal float");
  hp.player=true; next=hp; next.current=44;
  Check(DetectHealing(hp,next).amount==0,"suppress small remote-player regen");
  next.current=45;
  Check(DetectHealing(hp,next).amount==5,"remote player heal at threshold");
  hp.self=true; hp.maximum=10000; hp.current=5000; next=hp; next.current=6000;
  gain=DetectHealing(hp,next);
  Check(gain.amount==1000 && !gain.percent,"self healing uses real hit points");
  hp.current=0;
  Check(DetectHealing(hp,next).amount==0,"resurrection does not become a heal");
  auto arrow=HeadingArrow({10,20},0,7);
  Check(Near(arrow[0].x,10)&&Near(arrow[0].y,13),"north-facing arrow");
  arrow=HeadingArrow({10,20},128,7);
  Check(Near(arrow[0].x,3)&&Near(arrow[0].y,20),"west-facing arrow");
  Check(PlayerArrowColor(12)==PlayerArrowColor(12)&&PlayerArrowColor(12)!=PlayerArrowColor(13),
        "stable distinct player arrow colors");
  MinimapGesture click;
  Check(!click.Press(10,20,1000,500,4,4)&&click.preview&&!click.dragging,
        "single press previews without cursor capture");
  click.Release(1100);
  Check(!click.preview&&!click.pressed,"release hides held map");
  Check(click.Press(12,22,1250,500,4,4)&&click.dragging&&!click.preview,
        "double click and hold captures minimap move");
  click.Release(1300);
  Check(!click.dragging&&!click.armed,"drag release ends capture without rearming");
  click.Press(10,20,2000,500,4,4); click.Release(2050);
  Check(!click.Press(50,20,2100,500,4,4),"distant click cannot start a drag");
  click.Cancel();
  Check(!click.pressed&&!click.preview&&!click.dragging&&!click.armed,
        "focus/capture cancellation clears gesture");
  click.Press(10,20,3000,500,4,4); click.Release(3100);
  Check(!click.Press(10,20,3700,500,4,4),"expired double click previews normally");
  Check(!AttackFlashWhite(0,15.5f)&&AttackFlashWhite(65,15.5f)&&!AttackFlashWhite(130,15.5f),
        "pink-white attack flash changes about 15.5 times per second");
  Check(!AttackFlashWhite(1000,0)&&!AttackFlashWhite(1000,std::numeric_limits<float>::quiet_NaN()),
        "invalid flash rates do not corrupt animation");
  click.Cancel(); click.Press(10,20,4000,500,4,4); click.Release(5000);
  Check(!click.Press(10,20,5100,500,4,4),"long first hold cannot become an OS double click");
  Check(!ShowManaBar(false,false,true)&&!ShowStaminaBar(false),"NPCs never receive mana or stamina bars");
  Check(ShowManaBar(true,true,false)&&ShowManaBar(true,false,true)&&!ShowManaBar(true,false,false),
        "mana is restricted to self and grouped players");
  Check(ShowStaminaBar(true),"player stamina remains available");
  Check(PetOwnerCaption("Aggatha")=="(Aggatha's pet)","pet caption uses the real owner name");
  Check(PetOwnerCaption("").empty()&&PetOwnerCaption("bad\nname").empty()&&PetOwnerCaption(std::string(64,'a')).empty(),
        "reject unknown or malformed owner names");
  Check(PetNameplate("a wolf", "Aggatha")=="a wolf\n(Aggatha's pet)","owner line belongs to native nameplate");
  Check(PetNameplate("a wolf\n(Aggatha's Pet)","Bryn")=="a wolf\n(Bryn's pet)","charm owner changes replace old captions");
  Check(PetNameplate("a wolf\n(Aggatha's pet)","")=="a wolf","charm break removes ownership caption");
  Check(PetNameplate("a wolf\n(Quest)","Aggatha")=="a wolf\n(Quest)\n(Aggatha's pet)","preserve ordinary subtitle lines");
  Check(AttackNameplate("a wolf\n(Aggatha's pet)",true)==">>a wolf<<\n(Aggatha's pet)","attack brackets decorate only NPC name");
  Check(AttackNameplate(">>a wolf<<\n(Aggatha's pet)",false)=="a wolf\n(Aggatha's pet)","stopping attack preserves owner subtitle");
  Check(Near(ArrowScaleFromSlider(10,10,100),.5f)&&Near(ArrowScaleFromSlider(110,10,100),3),"slider endpoints");
  Check(Near(ArrowScaleFromSlider(-10,10,100),.5f)&&Near(ArrowScaleFromSlider(200,10,100),3),"slider clamps outside its track");
  Check(Near(ArrowScaleFromSlider(30,10,100),1),"slider normal size at twenty percent of track");
  a={-10,0};b={10,0};
  Check(ClipRect(a,b,4,3)&&Near(a.x,-4)&&Near(b.x,4),"external map clips crossing lines");
  a={-10,10};b={10,10};Check(!ClipRect(a,b,4,3),"external map rejects offscreen lines");
  unsigned random = 777;
  for (int i = 0; i < 10000; ++i) {
    std::string fuzz;
    for (int j = 0; j < i % 300; ++j) {
      random = random * 1664525u + 1013904223u;
      fuzz += char(random >> 24);
    }
    ParseMap(fuzz, i % 4);
  }
  std::printf("PASS: %d HUD model checks; 10000 malformed-map inputs\n",
              checks);
}
