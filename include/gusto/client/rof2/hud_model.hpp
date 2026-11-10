#pragma once
// Map projection / clipping and presentation rules adapted from Zeal and
// Hailstone (MIT). See THIRD_PARTY_NOTICES.md.
#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <vector>
namespace gusto::client::rof2::hud {
constexpr float Pi = 3.14159265358979323846f;
constexpr float MapRanges[] = {150, 300, 600, 1200, 2400};
struct Point {
  float x = 0, y = 0, z = 0;
};
struct Line {
  Point a, b;
  std::uint32_t color = 0xffffffff;
  int layer = 0;
};
struct Label {
  Point p;
  std::uint32_t color = 0xffffffff;
  int size = 1, layer = 0;
  std::string text;
};
struct Map {
  std::vector<Line> lines;
  std::vector<Label> labels;
};
Map ParseMap(std::string_view text, int layer);
bool ValidZoneName(std::string_view name);
Point ProjectMap(Point mapPoint, Point playerWorld, float heading, float radius,
                 float range, bool rotate);
bool ClipCircle(Point &a, Point &b, float radius);
bool ClipRect(Point &a, Point &b, float halfW, float halfH);
float HeightAlpha(float z, float feet, bool enabled, float above, float below);
float Smooth(float current, float target, float dt, float seconds);
float SmoothAngle(float current, float target, float dt, float seconds);
// Resource values absent from the client must never become a fabricated bar.
float ResourceRatio(int current, int maximum);
bool ShowManaBar(bool player, bool self, bool grouped);
bool ShowStaminaBar(bool player);
std::string PetOwnerCaption(std::string_view ownerName);
std::string PetNameplate(std::string_view base, std::string_view ownerName);
std::string AttackNameplate(std::string_view base, bool attacking);
float ArrowScaleFromSlider(float x, float left, float width);
bool AttackFlashWhite(std::uint64_t elapsedMs, float changesPerSecond);
std::uint32_t PlayerArrowColor(std::uint32_t id);
std::array<Point, 3> HeadingArrow(Point center, float heading, float size);
struct HealthSample {
  std::uintptr_t identity = 0;
  int current = 0, maximum = 0;
  bool player = false, pet = false, self = false;
};
struct HealingGain { int amount = 0; bool percent = false; };
HealingGain DetectHealing(const HealthSample &before, const HealthSample &after);
// A first press previews the map. Only a second nearby press captures a drag.
struct MinimapGesture {
  bool pressed = false, dragging = false, preview = false, armed = false;
  float lastX = 0, lastY = 0;
  std::uint64_t lastDown = 0;
  bool Press(float x, float y, std::uint64_t now, unsigned doubleMs,
             float toleranceX, float toleranceY);
  void Release(std::uint64_t now);
  void Cancel();
};
struct FloatEffect {
  std::uint32_t target = 0, source = 0;
  std::uintptr_t identity = 0;
  int damage = 0, spell = -1;
  std::uint64_t born = 0;
  bool critical = false, healing = false, percent = false;
  float x = 0, y = 0;
  float Life(std::uint64_t now) const;
  float Alpha(std::uint64_t now) const;
  float Rise(std::uint64_t now) const;
};
} // namespace gusto::client::rof2::hud
