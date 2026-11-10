#include "gusto/client/rof2/hud_model.hpp"
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
namespace gusto::client::rof2::hud {
bool ValidZoneName(std::string_view s) {
  if (s.empty() || s.size() > 127)
    return false;
  for (unsigned char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_'))
      return false;
  return true;
}
Map ParseMap(std::string_view text, int layer) {
  Map out;
  if (layer < 0 || layer > 3 || text.size() > 16 * 1024 * 1024)
    return out;
  std::istringstream input{std::string(text)};
  input.imbue(std::locale::classic());
  std::string row;
  auto finite = [](Point p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
           std::fabs(p.x) < 1e7f && std::fabs(p.y) < 1e7f &&
           std::fabs(p.z) < 1e7f;
  };
  while (std::getline(input, row) &&
         out.lines.size() + out.labels.size() < 100000) {
    if (!row.empty() && row.back() == '\r')
      row.pop_back();
    if (row.size() > 1024)
      continue;
    std::replace(row.begin(), row.end(), ',', ' ');
    std::istringstream s(row);
    s.imbue(std::locale::classic());
    char type = 0;
    s >> type;
    int r = 0, g = 0, b = 0;
    auto color = [&] {
      return 0xff000000u | (std::uint32_t(std::clamp(r, 0, 255)) << 16) |
             (std::uint32_t(std::clamp(g, 0, 255)) << 8) |
             std::uint32_t(std::clamp(b, 0, 255));
    };
    if (type == 'L' || type == 'l') {
      Line l;
      l.layer = layer;
      if (s >> l.a.x >> l.a.y >> l.a.z >> l.b.x >> l.b.y >> l.b.z >> r >> g >>
              b &&
          finite(l.a) && finite(l.b)) {
        l.color = color();
        out.lines.push_back(l);
      }
    } else if (type == 'P' || type == 'p') {
      Label l;
      l.layer = layer;
      if (s >> l.p.x >> l.p.y >> l.p.z >> r >> g >> b >> l.size &&
          finite(l.p)) {
        std::getline(s >> std::ws, l.text);
        if (l.text.empty() || l.text.size() > 256)
          continue;
        std::replace(l.text.begin(), l.text.end(), '_', ' ');
        l.size = std::clamp(l.size, 1, 3);
        l.color = color();
        out.labels.push_back(l);
      }
    }
  }
  return out;
}
Point ProjectMap(Point p, Point player, float heading, float radius,
                 float range, bool rotate) {
  float x = (p.x + player.x) * radius / std::max(range, 1.f),
        y = (p.y + player.y) * radius / std::max(range, 1.f);
  const float a = rotate ? heading * 2 * Pi / 512.f : 0, cs = std::cos(a),
              sn = std::sin(a);
  return {x * cs - y * sn, x * sn + y * cs, p.z};
}
bool ClipCircle(Point &a, Point &b, float r) {
  if (!std::isfinite(r) || r <= 0 || !std::isfinite(a.x) ||
      !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y))
    return false;
  const float dx = b.x - a.x, dy = b.y - a.y, aa = dx * dx + dy * dy;
  const float cc = a.x * a.x + a.y * a.y - r * r;
  if (aa < 1e-12f)
    return cc <= 0;
  const float bb = 2 * (a.x * dx + a.y * dy), disc = bb * bb - 4 * aa * cc;
  if (disc < 0)
    return false;
  const float root = std::sqrt(disc),
              lo = std::max(0.f, (-bb - root) / (2 * aa)),
              hi = std::min(1.f, (-bb + root) / (2 * aa));
  if (lo > hi)
    return false;
  Point start = a;
  a.x = start.x + lo * dx;
  a.y = start.y + lo * dy;
  b.x = start.x + hi * dx;
  b.y = start.y + hi * dy;
  return true;
}
bool ClipRect(Point &a, Point &b, float w, float h) {
  if (!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(b.x)||!std::isfinite(b.y)||w<=0||h<=0) return false;
  float dx = b.x - a.x, dy = b.y - a.y, lo = 0, hi = 1;
  auto edge = [&](float p, float q) {
    if (std::fabs(p) < 1e-12f)
      return q >= 0;
    float t = q / p;
    if (p < 0)
      lo = std::max(lo, t);
    else
      hi = std::min(hi, t);
    return lo <= hi;
  };
  if (!edge(-dx, a.x + w) || !edge(dx, w - a.x) || !edge(-dy, a.y + h) ||
      !edge(dy, h - a.y))
    return false;
  Point start = a;
  a.x = start.x + lo * dx;
  a.y = start.y + lo * dy;
  b.x = start.x + hi * dx;
  b.y = start.y + hi * dy;
  return true;
}
float HeightAlpha(float z, float feet, bool enabled, float above, float below) {
  if (!enabled)
    return 1;
  float d = z - feet, range = std::max(1.f, d >= 0 ? above : below),
        distance = std::fabs(d);
  return distance <= range
             ? 1.f
             : std::max(.15f, 1.f - (distance - range) / (2 * range) * .85f);
}
float Smooth(float current, float target, float dt, float seconds) {
  if (!std::isfinite(current) || !std::isfinite(target))
    return 0;
  if (!std::isfinite(dt) || !std::isfinite(seconds))
    return current;
  if (seconds <= 0)
    return target;
  return current + (target - current) *
                       (1 - std::exp(-std::clamp(dt, 0.f, .25f) / seconds));
}
float SmoothAngle(float current, float target, float dt, float seconds) {
  float delta = std::remainder(target - current, 512.f);
  return current + Smooth(0, delta, dt, seconds);
}
bool ShowManaBar(bool player, bool self, bool grouped) { return player && (self || grouped); }
bool ShowStaminaBar(bool player) { return player; }
std::string PetOwnerCaption(std::string_view name) {
  if (name.empty() || name.size()>63) return {};
  for (unsigned char c:name) if (c<32 || c==127) return {};
  return "("+std::string(name)+"'s pet)";
}
std::string PetNameplate(std::string_view base,std::string_view owner) {
  std::string result;
  while (!base.empty()) {
    auto split=base.find('\n'); auto line=base.substr(0,split);
    const bool ownerLine=line.starts_with("(") && (line.ends_with("'s pet)") || line.ends_with("'s Pet)"));
    if (!ownerLine) { if (!result.empty()) result+='\n'; result+=line; }
    if (split==base.npos) break;
    base.remove_prefix(split+1);
  }
  auto caption=PetOwnerCaption(owner);
  if (!caption.empty()) { if (!result.empty()) result+='\n'; result+=caption; }
  return result;
}
std::string AttackNameplate(std::string_view base,bool attack) {
  auto split=base.find('\n'); auto first=base.substr(0,split);
  if (first.starts_with(">>") && first.ends_with("<<") && first.size()>=4)
    first=first.substr(2,first.size()-4);
  std::string result=attack ? ">>"+std::string(first)+"<<" : std::string(first);
  if (split!=base.npos) result+=base.substr(split);
  return result;
}
float ArrowScaleFromSlider(float x,float left,float width) {
  if (!std::isfinite(x)||!std::isfinite(left)||!std::isfinite(width)||width<=0) return 1;
  return .5f+std::clamp((x-left)/width,0.f,1.f)*2.5f;
}
float ResourceRatio(int current, int maximum) {
  if (maximum <= 0 || current < 0) return -1;
  return std::clamp(float(current) / float(maximum), 0.f, 1.f);
}
bool AttackFlashWhite(std::uint64_t elapsedMs, float changesPerSecond) {
  if (!std::isfinite(changesPerSecond) || changesPerSecond<=0) return false;
  const double phase=std::fmod(double(elapsedMs)*double(changesPerSecond)/1000.,2.);
  return phase>=1.;
}
std::uint32_t PlayerArrowColor(std::uint32_t id) {
  const double hue = std::fmod(double(id) * .6180339887498949, 1.) * 6.;
  const int sector = int(hue);
  const float f = float(hue - sector), low = .28f, high = .96f;
  const float up = low + (high - low) * f, down = high - (high - low) * f;
  float r = 0, g = 0, b = 0;
  switch (sector) {
  case 0: r=high; g=up; b=low; break;
  case 1: r=down; g=high; b=low; break;
  case 2: r=low; g=high; b=up; break;
  case 3: r=low; g=down; b=high; break;
  case 4: r=up; g=low; b=high; break;
  default:r=high; g=low; b=down; break;
  }
  return 0xff000000u | (std::uint32_t(r*255)<<16) |
         (std::uint32_t(g*255)<<8) | std::uint32_t(b*255);
}
std::array<Point,3> HeadingArrow(Point c, float heading, float size) {
  const float a = heading * 2 * Pi / 512, x = -std::sin(a), y = -std::cos(a);
  return {Point{c.x+x*size,c.y+y*size},
          Point{c.x-x*size*.55f+y*size*.55f,c.y-y*size*.55f-x*size*.55f},
          Point{c.x-x*size*.55f-y*size*.55f,c.y-y*size*.55f+x*size*.55f}};
}
HealingGain DetectHealing(const HealthSample &a, const HealthSample &b) {
  if (!a.identity || a.identity != b.identity || a.maximum != b.maximum ||
      a.current <= 0 || b.current <= a.current ||
      ResourceRatio(a.current,a.maximum) < 0 ||
      ResourceRatio(b.current,b.maximum) < 0 || b.current > b.maximum)
    return {};
  if (b.self) return {b.current-a.current, false};
  const int delta = int((std::int64_t(b.current)*100)/b.maximum -
                        (std::int64_t(a.current)*100)/a.maximum);
  // Zeal suppresses small remote-player/pet changes that can be regeneration.
  if (delta <= 0 || ((b.player || b.pet) && delta < 5)) return {};
  return {delta, true};
}
bool MinimapGesture::Press(float x, float y, std::uint64_t now,
                           unsigned doubleMs, float tx, float ty) {
  dragging = armed && now >= lastDown && now-lastDown <= doubleMs &&
             std::fabs(x-lastX) <= tx && std::fabs(y-lastY) <= ty;
  pressed = true;
  preview = !dragging;
  armed = false;
  lastX=x; lastY=y; lastDown=now;
  return dragging;
}
void MinimapGesture::Release(std::uint64_t now) {
  armed = pressed && !dragging && now>=lastDown;
  pressed=dragging=preview=false;
}
void MinimapGesture::Cancel() { pressed=dragging=preview=armed=false; }
float FloatEffect::Life(std::uint64_t now) const {
  return now >= born ? float(now - born) / float(critical ? 3000 : 2500) : 0;
}
float FloatEffect::Alpha(std::uint64_t now) const {
  const float elapsed = now >= born ? float(now - born) : 0;
  return std::clamp(1.f - elapsed / (critical ? 5000.f : 2500.f), 0.f, 1.f);
}
float FloatEffect::Rise(std::uint64_t now) const {
  return now >= born ? float(now - born) * .08f : 0.f;
}
} // namespace gusto::client::rof2::hud
