// Zeal-inspired map/combat/camera features with Hailstone's modern minimap
// layout and Midnight palette. MIT reference attributions in
// THIRD_PARTY_NOTICES.md.
#include "gusto/client/rof2/hud_overlay.hpp"
#include "gusto/client/rof2/addresses.hpp"
#include "gusto/client/rof2/globals.hpp"
#include "gusto/client/rof2/hook_utils.hpp"
#include "gusto/client/rof2/hud_model.hpp"
#include "gusto/client/rof2/life_fx.hpp"
#include "gusto/client/rof2/gamepad.hpp"
#include "gusto/client/rof2/named_pipe.hpp"
#include "gusto/client/rof2/hud_external_map.hpp"
#include "gusto/client/rof2/nameplate_hooks.hpp"
#include "gusto/client/rof2/tag_hooks.hpp"
#include "gusto/client/rof2/initialization.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <windowsx.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
namespace gusto::client::rof2 {
namespace {
using hud::Point;
HMODULE game = nullptr;
ValidatedClient token;
SRWLOCK hudLock = SRWLOCK_INIT;
struct Guard {
  Guard() { AcquireSRWLockExclusive(&hudLock); }
  ~Guard() { ReleaseSRWLockExclusive(&hudLock); }
};
struct Settings {
  bool minimap = true, rotate = false, autoZ = true, group = true,
       labels = true, floating = true;
  bool melee = true, spells = true, self = true, others = false, camera = false;
  bool healing = true, players = true, pets = true, petOwnerNames = true;
  bool hpBars = true, manaBars = true, staminaBars = true;
  bool barPlayers = true, barNpcs = true, barPets = true, barSelf = true;
  bool barsTargetOnly = false;
  float barRange = 250, arrowScale = 1,barWidth=48,barHeight=4;
  std::array<float,4> barWidths{48,48,48,48},barHeights{6,4,4,6};bool barRpg=true;
  std::uint32_t hpColor=0xff35d85b,manaColor=0xff468dff,staminaColor=0xffffd45e,npcHpColor=0xff35d85b;
  LifeOptions life;
  int zoom = 2, above = 30, below = 30, bigHit = 100, layers = 15;
  float x = -1, y = 40, scale = 1, smoothing = .12f, fov = 45, maxZoom = 100;
} cfg;
hud::MinimapGesture mapGesture;
bool menu = false, fullMap = false, dragging = false, dragMoved = false,
     ownedLeft = false, ownedRight = false;
ULONGLONG wheelMessageTick = 0, blockLeftUntil = 0, blockRightUntil = 0;
float dragX = 0, dragY = 0;
POINT mouse{};
HWND window = nullptr;
WNDPROC previousWndProc = nullptr;
std::filesystem::path settingsPath;
std::string zoneName, mapZone, lastError;
hud::Map map;
std::shared_ptr<const hud::Map> externalMapData;
std::vector<hud::FloatEffect> effects;
struct HudSpawn {
  void *object = nullptr;
  std::uint32_t id = 0, owner = 0;
  BYTE type = 0;
  Point position{};
  float heading = 0;
  int hp = -1, hpMax = 0, mana = -1, manaMax = 0, stamina = -1, staminaMax = 0;
};
std::vector<HudSpawn> hudSpawns;
std::unordered_map<std::uint32_t, hud::HealthSample> previousHealth;
ULONGLONG spawnTick = 0;
int menuPage = 0, padInput=0,barEdit=0,padCategory=0,padPickerPage=0,padModifiers=0;
bool padPicker=false,padDevicePage=false,padListening=false,padKeyListening=false;
std::string padBindingFeedback;
std::vector<int> PadCommands(){
 if(padCategory==6)return {pad::MouseLeft,pad::MouseRight,pad::MouseMiddle,-1};
 std::vector<int> result;
 for(int key=8;key<255;++key){
  if(!pad::KeyBinding(key))continue;
  if(padCategory==1 && !(key>='A'&&key<='Z'))continue;
  if(padCategory==2 && !(key>='0'&&key<='9'))continue;
  if(padCategory==3 && !(key>=VK_F1&&key<=VK_F24))continue;
  if(padCategory==4 && !(key<=VK_DELETE || key==VK_NUMLOCK || key==VK_SCROLL))continue;
  if(padCategory==5 && !(key>=VK_NUMPAD0&&key<=VK_DIVIDE))continue;
  result.push_back(key|padModifiers);
 }
 return result;
}

constexpr std::uint32_t barColors[]={0xff35d85b,0xff468dff,0xffffd45e,0xffffffff,0xffff8024,0xffff70c9,0xffb68bff,0xffff5555,0xff55ddd0};
const char* barColorNames[]={"Green","Blue","Gold","White","Orange","Pink","Purple","Red","Teal"};
int BarColorIndex(std::uint32_t color){for(int i=0;i<9;++i)if(barColors[i]==color)return i;return -1;}
std::string BarColorName(std::uint32_t color){int index=BarColorIndex(color);return index>=0?barColorNames[index]:"Custom RGB";}

constexpr std::uint32_t ghostPalette[]={0xffffffff,0xffff8024,0xff55bfff,0xff65ed96,0xffff70c9,0xffb68bff,0xffffdf66,0xffff5555};
const char* ghostColorNames[]={"White","Orange","Blue","Green","Pink","Purple","Yellow","Red"};
int GhostColorIndex(std::uint32_t color){for(int i=0;i<8;++i)if(ghostPalette[i]==color)return i;return 0;}
hud::Point lifeViewer; bool lifeViewerValid=false;
int skeletonLaughId=-1;
int localManaCurrent = -1, localManaMaximum = 0;

std::atomic_bool initialized{false};
std::atomic_ulong hudFrames{0};
using HitFn = bool(__thiscall *)(void *, void *, bool, int);
using MouseFn = void(__thiscall *)(void *, int, int);
using WheelFn = void(__cdecl *)(int);
using CameraFn = void(__thiscall *)(void *);
using ProcessMouseFn = void(__cdecl *)();
ProcessMouseFn originalProcessMouse = nullptr;
using ProcessKeyboardFn=int(__cdecl*)();
ProcessKeyboardFn originalProcessKeyboard=nullptr;
MouseFn originalLeftDown = nullptr, originalRightDown = nullptr;
bool flushNativeMouse = false;
std::atomic_bool worldLeftHeld{false}, worldRightHeld{false};
HitFn originalHit = nullptr;
MouseFn originalLeft = nullptr, originalRight = nullptr;
WheelFn originalWheel = nullptr;
CameraFn originalCamera = nullptr;
float originalMaxZoom = 53, lastMaxZoom = 53;
bool zoomOwned = false;
void *lastCamera = nullptr;
float savedFov = 45;
bool ownedFov = false;
Point smoothPos{}, smoothAngles{};
ULONGLONG cameraTick = 0;
std::uint32_t localId = 0, petId = 0;
constexpr addresses::Global EverQuest{0xe67ccc,
                                      addresses::Storage::PointerStorage};
constexpr addresses::Global SpawnManager{0xe641d0,
                                         addresses::Storage::PointerStorage};
constexpr addresses::Global LocalPC{0xdd261c,
                                    addresses::Storage::PointerStorage};
constexpr addresses::Global WorldData{0xdd2600,
                                      addresses::Storage::PointerStorage};
void *Global(addresses::Global a, std::size_t size = 4) {
  return ResolvePointer(game, token, a, size);
}
template <class T> bool Read(const void *p, std::size_t off, T &out) {
  auto a = reinterpret_cast<std::uintptr_t>(p);
  if (!a || off > UINTPTR_MAX - a)
    return false;
  SIZE_T got = 0;
  return ReadProcessMemory(GetCurrentProcess(),
                           reinterpret_cast<void *>(a + off), &out, sizeof(out),
                           &got) &&
         got == sizeof(out);
}
bool InGame() {
  int state = 0;
  return initialized && Read(Global(EverQuest), 0x5c8, state) && state == 5;
}
std::uintptr_t VA(std::uintptr_t va, std::size_t n = 4) {
  return token.IsValidFor(game) ? ResolvePreferredVA(game, va, n) : 0;
}
bool StoreFloat(void *p, float f) {
  DWORD old = 0;
  if (!p || !std::isfinite(f) || !VirtualProtect(p, 4, PAGE_READWRITE, &old))
    return false;
  std::memcpy(p, &f, 4);
  DWORD unused;
  VirtualProtect(p, 4, old, &unused);
  return true;
}
void Save() {
  if (settingsPath.empty())
    return;
  std::wstring section;
  auto put = [&](const wchar_t *key, auto value) {
    section += key; section += L"="; section += std::to_wstring(value);
    section.push_back(L'\0');
  };
  SaveGamepad(settingsPath.c_str());
  SavePipes(settingsPath.c_str());
  SetLifeOptions(cfg.life);
  put(L"DeathNPC",cfg.life.npc);put(L"DeathPlayer",cfg.life.player);put(L"FeignJester",cfg.life.feign);put(L"FeignLaugh",cfg.life.laugh);
  put(L"LifeDuration",cfg.life.duration);put(L"LifeRise",cfg.life.rise);put(L"GhostNPCScale",cfg.life.npcScale);
  put(L"GhostPlayerScale",cfg.life.playerScale);put(L"JesterScale",cfg.life.jesterScale);put(L"LifeRange",cfg.life.range);
  put(L"GhostNPCColor",cfg.life.npcColor & 0xffffffu);put(L"GhostPlayerColor",cfg.life.playerColor & 0xffffffu);
  put(L"ArrowScale",cfg.arrowScale);
  put(L"PetOwnerNames",cfg.petOwnerNames);
  put(L"Healing",cfg.healing); put(L"Players",cfg.players); put(L"Pets",cfg.pets);
  put(L"HPBars",cfg.hpBars); put(L"ManaBars",cfg.manaBars); put(L"StaminaBars",cfg.staminaBars);
  put(L"BarPlayers",cfg.barPlayers); put(L"BarNPCs",cfg.barNpcs); put(L"BarPets",cfg.barPets);
  const wchar_t* widths[]={L"HPBarWidth",L"ManaBarWidth",L"StaminaBarWidth",L"NPCHPBarWidth"};
  const wchar_t* heights[]={L"HPBarHeight",L"ManaBarHeight",L"StaminaBarHeight",L"NPCHPBarHeight"};
  for(int i=0;i<4;++i){put(widths[i],cfg.barWidths[i]);put(heights[i],cfg.barHeights[i]);}put(L"RpgBars",cfg.barRpg);
  put(L"BarWidth",cfg.barWidth);put(L"BarHeight",cfg.barHeight);
  put(L"NPCHPBarColor",cfg.npcHpColor&0xffffffu);put(L"HPBarColor",cfg.hpColor&0xffffffu);put(L"ManaBarColor",cfg.manaColor&0xffffffu);put(L"StaminaBarColor",cfg.staminaColor&0xffffffu);
  put(L"BarSelf",cfg.barSelf); put(L"BarsTargetOnly",cfg.barsTargetOnly); put(L"BarRange",cfg.barRange);
  auto names=GetNameplateOptions();
  put(L"ConColors",names.conColors); put(L"PinkTarget",names.pinkTarget);
  put(L"Brackets",names.brackets); put(L"AttackFlash",names.attackFlash);
  put(L"FlashRate",names.flashChangesPerSecond);
  auto tags=GetTagDisplayOptions();
  put(L"Tags",tags.enabled); put(L"TagModels",tags.models); put(L"TagText",tags.text);
  put(L"TagDefaultArrow",tags.defaultArrow); put(L"TagFilter",tags.filter);
  put(L"TagSuppress",tags.suppress); put(L"TagPrettyprint",tags.prettyprint);
  put(L"Minimap", cfg.minimap);
  put(L"Rotate", cfg.rotate);
  put(L"AutoZ", cfg.autoZ);
  put(L"Group", cfg.group);
  put(L"Labels", cfg.labels);
  put(L"Floating", cfg.floating);
  put(L"Melee", cfg.melee);
  put(L"Spells", cfg.spells);
  put(L"Self", cfg.self);
  put(L"Others", cfg.others);
  put(L"Camera", cfg.camera);
  put(L"Zoom", cfg.zoom);
  put(L"Above", cfg.above);
  put(L"Below", cfg.below);
  put(L"BigHit", cfg.bigHit);
  put(L"Layers", cfg.layers);
  put(L"X", cfg.x);
  put(L"Y", cfg.y);
  put(L"Scale", cfg.scale);
  put(L"Smoothing", cfg.smoothing);
  put(L"Fov", cfg.fov);
  put(L"MaxZoom", cfg.maxZoom);
  section.push_back(L'\0');
  if (!WritePrivateProfileSectionW(L"GustoHUD",section.c_str(),settingsPath.c_str()))
    lastError="HUD settings could not be saved";
}
void Load() {
  wchar_t path[32768]{};
  DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, 32768);
  if (!n || n >= 32768)
    return;
  auto dir = std::filesystem::path(path) / L"Gusto";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec)
    return;
  settingsPath = dir / L"hud.ini";
  auto get = [&](const wchar_t *key, int fallback) {
    return int(GetPrivateProfileIntW(L"GustoHUD", key, fallback,
                                     settingsPath.c_str()));
  };
  auto number = [&](const wchar_t *key, float fallback, float lo, float hi) {
    wchar_t b[64]{};
    GetPrivateProfileStringW(L"GustoHUD", key, L"", b, 64,
                             settingsPath.c_str());
    wchar_t *end;
    float f = wcstof(b, &end);
    return b[0] && end != b && !*end && std::isfinite(f) ? std::clamp(f, lo, hi)
                                                         : fallback;
  };
  LoadGamepad(settingsPath.c_str());
  LoadPipes(settingsPath.c_str());
  cfg.life.npcColor=0xff000000u|(get(L"GhostNPCColor",0xffffff)&0xffffffu);
  cfg.life.playerColor=0xff000000u|(get(L"GhostPlayerColor",0xff8024)&0xffffffu);
  cfg.life.npc=get(L"DeathNPC",1)!=0;cfg.life.player=get(L"DeathPlayer",1)!=0;
  cfg.life.feign=get(L"FeignJester",1)!=0;cfg.life.laugh=get(L"FeignLaugh",1)!=0;
  cfg.life.duration=number(L"LifeDuration",8,4,20);cfg.life.rise=number(L"LifeRise",1.1f,.25f,3);
  cfg.life.npcScale=number(L"GhostNPCScale",1.5f,.5f,4);cfg.life.playerScale=number(L"GhostPlayerScale",2.8f,1,6);
  cfg.life.jesterScale=number(L"JesterScale",2,.5f,5);cfg.life.range=number(L"LifeRange",250,25,1000);SetLifeOptions(cfg.life);
  cfg.arrowScale=number(L"ArrowScale",1,.5f,3);
  cfg.petOwnerNames=get(L"PetOwnerNames",1)!=0;
  SetNativePetOwnerNames(cfg.petOwnerNames);
  cfg.healing=get(L"Healing",1)!=0; cfg.players=get(L"Players",1)!=0; cfg.pets=get(L"Pets",1)!=0;
  cfg.hpBars=get(L"HPBars",1)!=0; cfg.manaBars=get(L"ManaBars",1)!=0; cfg.staminaBars=get(L"StaminaBars",1)!=0;
  cfg.barPlayers=get(L"BarPlayers",1)!=0; cfg.barNpcs=get(L"BarNPCs",1)!=0; cfg.barPets=get(L"BarPets",1)!=0;
  cfg.barSelf=get(L"BarSelf",1)!=0; cfg.barsTargetOnly=get(L"BarsTargetOnly",0)!=0;
  cfg.barWidth=number(L"BarWidth",48,24,160);cfg.barHeight=number(L"BarHeight",4,2,12);
  const wchar_t* widths[]={L"HPBarWidth",L"ManaBarWidth",L"StaminaBarWidth",L"NPCHPBarWidth"};
  const wchar_t* heights[]={L"HPBarHeight",L"ManaBarHeight",L"StaminaBarHeight",L"NPCHPBarHeight"};
  for(int i=0;i<3;++i){cfg.barWidths[i]=number(widths[i],cfg.barWidth,24,160);cfg.barHeights[i]=number(heights[i],cfg.barHeight,2,16);}cfg.barWidths[3]=number(widths[3],cfg.barWidths[0],24,160);cfg.barHeights[3]=number(heights[3],cfg.barHeights[0],2,16);cfg.barRpg=get(L"RpgBars",1)!=0;
  cfg.npcHpColor=0xff000000u|(get(L"NPCHPBarColor",get(L"HPBarColor",0x35d85b))&0xffffffu);
  cfg.hpColor=0xff000000u|(get(L"HPBarColor",0x35d85b)&0xffffffu);cfg.manaColor=0xff000000u|(get(L"ManaBarColor",0x468dff)&0xffffffu);cfg.staminaColor=0xff000000u|(get(L"StaminaBarColor",0xffd45e)&0xffffffu);
  cfg.barRange=number(L"BarRange",250,25,1000);
  SetNameplateOptions({get(L"ConColors",1)!=0,get(L"PinkTarget",1)!=0,
    get(L"Brackets",1)!=0,get(L"AttackFlash",1)!=0,number(L"FlashRate",15.5f,1,16)});
  auto tags=GetTagDisplayOptions();
  tags.enabled=get(L"Tags",tags.enabled)!=0; tags.models=get(L"TagModels",1)!=0;
  tags.text=get(L"TagText",1)!=0; tags.defaultArrow=get(L"TagDefaultArrow",tags.defaultArrow)!=0;
  tags.filter=get(L"TagFilter",tags.filter)!=0; tags.suppress=get(L"TagSuppress",tags.suppress)!=0;
  tags.prettyprint=get(L"TagPrettyprint",tags.prettyprint)!=0; SetTagDisplayOptions(tags);
  cfg.minimap = get(L"Minimap", 1) != 0;
  cfg.rotate = get(L"Rotate", 0) != 0;
  cfg.autoZ = get(L"AutoZ", 1) != 0;
  cfg.group = get(L"Group", 1) != 0;
  cfg.labels = get(L"Labels", 1) != 0;
  cfg.floating = get(L"Floating", 1) != 0;
  cfg.melee = get(L"Melee", 1) != 0;
  cfg.spells = get(L"Spells", 1) != 0;
  cfg.self = get(L"Self", 1) != 0;
  cfg.others = get(L"Others", 0) != 0;
  cfg.camera = get(L"Camera", 0) != 0;
  cfg.zoom = std::clamp(get(L"Zoom", 2), 0, 4);
  cfg.above = std::clamp(get(L"Above", 30), 1, 1000);
  cfg.below = std::clamp(get(L"Below", 30), 1, 1000);
  cfg.bigHit = std::clamp(get(L"BigHit", 100), 1, 100000000);
  cfg.layers = get(L"Layers", 15) & 15;
  cfg.x = number(L"X", -1, -1, 16000);
  cfg.y = number(L"Y", 40, 0, 16000);
  cfg.scale = number(L"Scale", 1, .5f, 3);
  cfg.smoothing = number(L"Smoothing", .12f, 0, .5f);
  cfg.fov = number(L"Fov", 45, 10, 100);
  cfg.maxZoom = number(L"MaxZoom", 100, 5, 200);
}
void UpdateZoom() {
  auto p = reinterpret_cast<void *>(VA(0x9d0de4));
  float current = 0;
  if (!Read(p, 0, current))
    return;
  float wanted = cfg.camera ? cfg.maxZoom : originalMaxZoom;
  if (current != lastMaxZoom && current != originalMaxZoom) {
    lastError = "Camera zoom constant is owned by another modification";
    return;
  }
  if (current != wanted && StoreFloat(p, wanted)) {
    lastMaxZoom = wanted;
    zoomOwned = cfg.camera;
  }
}

struct Vertex {
  float x, y, z, rhw;
  std::uint32_t color;
  float u, v;
};
class Canvas {
  IDirect3DDevice9 *device = nullptr;
  IDirect3DTexture9 *atlas = nullptr;
  float widths[3][96]{};
  std::vector<Vertex> vertices;
  float vertexDepth=0;bool depthEnabled=false;

public:
  ~Canvas() { Release(); }
  void Release() {
    if (atlas)
      atlas->Release();
    atlas = nullptr;
    device = nullptr;
    vertices.clear();
  }
  bool Init(IDirect3DDevice9 *d) {
    if (device == d && atlas)
      return true;
    Release();
    device = d;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
      return false;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 512;
    bi.bmiHeader.biHeight = -1024;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HBITMAP bmp =
        CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bmp) {
      DeleteDC(dc);
      return false;
    }
    auto oldBmp = SelectObject(dc, bmp);
    std::memset(pixels, 0, 512 * 1024 * 4);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    const int heights[3] = {13, 16, 24};
    for (int f = 0; f < 3; ++f) {
      HFONT font =
          CreateFontW(-heights[f], 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial");
      auto old = SelectObject(dc, font);
      for (int c = 32; c < 128; ++c) {
        char ch = char(c);
        SIZE size{};
        GetTextExtentPoint32A(dc, &ch, 1, &size);
        widths[f][c - 32] = float(size.cx);
        TextOutA(dc, ((c - 32) % 16) * 32 + 2,
                 f * 256 + ((c - 32) / 16) * 40 + 2, &ch, 1);
      }
      SelectObject(dc, old);
      DeleteObject(font);
    }
    GdiFlush();
    HRESULT hr = d->CreateTexture(512, 1024, 1, 0, D3DFMT_A8R8G8B8,
                                  D3DPOOL_MANAGED, &atlas, nullptr);
    if (SUCCEEDED(hr)) {
      D3DLOCKED_RECT dest{};
      hr = atlas->LockRect(0, &dest, nullptr, 0);
      if (SUCCEEDED(hr)) {
        for (int y = 0; y < 1024; ++y) {
          auto *row = reinterpret_cast<std::uint32_t *>(
              static_cast<BYTE *>(dest.pBits) + y * dest.Pitch);
          auto *src = static_cast<std::uint32_t *>(pixels) + y * 512;
          for (int x = 0; x < 512; ++x) {
            auto c = src[x];
            auto a = std::max({c & 255, (c >> 8) & 255, (c >> 16) & 255});
            row[x] = (a << 24) | 0xffffff;
          }
        }
        *static_cast<std::uint32_t *>(dest.pBits) = 0xffffffff;
        atlas->UnlockRect(0);
      }
    }
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    if (FAILED(hr)) {
      Release();
      return false;
    }
    return true;
  }
  void Begin() {
    vertices.clear();vertexDepth=0;depthEnabled=false;
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    device->SetTexture(0, atlas);
    device->SetStreamSourceFreq(0, 1);
    device->SetStreamSourceFreq(1, 1);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS,
                                 D3DTTFF_DISABLE);
    device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  }
  void Flush() {
    if (vertices.empty())
      return;
    auto hr =
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, UINT(vertices.size() / 3),
                                vertices.data(), sizeof(Vertex));
    if (FAILED(hr))
      lastError = "HUD DrawPrimitiveUP failed";
    vertices.clear();
  }
  void Depth(bool enabled,float z=0){
    if(enabled!=depthEnabled){Flush();depthEnabled=enabled;
      device->SetRenderState(D3DRS_ZENABLE,enabled?D3DZB_TRUE:D3DZB_FALSE);
      device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
      device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    }
    vertexDepth=z;
  }
  Vertex V(float x, float y, std::uint32_t c, float u = 0.5f / 512,
           float v = 0.5f / 1024) {
    return {x - .5f, y - .5f, vertexDepth, 1, c, u, v};
  }
  void Tri(Vertex a, Vertex b, Vertex c) {
    if (vertices.size() > 60000)
      Flush();
    vertices.insert(vertices.end(), {a, b, c});
  }
  void Rect(float x, float y, float w, float h, std::uint32_t c) {
    auto a = V(x, y, c), b = V(x + w, y, c), d = V(x, y + h, c),
         e = V(x + w, y + h, c);
    Tri(a, b, d);
    Tri(b, e, d);
  }
  void Line(float x, float y, float xx, float yy, std::uint32_t c,
            float width = 1.5f) {
    float dx = xx - x, dy = yy - y, len = std::hypot(dx, dy);
    if (len < .01f)
      return;
    float nx = -dy / len * width / 2, ny = dx / len * width / 2;
    auto a = V(x + nx, y + ny, c), b = V(xx + nx, yy + ny, c),
         d = V(x - nx, y - ny, c), e = V(xx - nx, yy - ny, c);
    Tri(a, b, d);
    Tri(b, e, d);
  }
  void Circle(float x, float y, float r, std::uint32_t c, bool fill = false,
              float width = 1.5f) {
    for (int i = 0; i < 96; ++i) {
      float a = i * 2 * hud::Pi / 96, b = (i + 1) * 2 * hud::Pi / 96;
      if (fill)
        Tri(V(x, y, c), V(x + std::cos(a) * r, y + std::sin(a) * r, c),
            V(x + std::cos(b) * r, y + std::sin(b) * r, c));
      else
        Line(x + std::cos(a) * r, y + std::sin(a) * r, x + std::cos(b) * r,
             y + std::sin(b) * r, c, width);
    }
  }
  float Width(std::string_view text, int font = 1, float scale = 1) {
    float w = 0;
    for (unsigned char c : text)
      w += widths[font][(c >= 32 && c < 128) ? c - 32 : '?' - 32] * scale;
    return w;
  }
  std::string Fit(std::string s, float w, int font, float scale) {
    while (!s.empty() && Width(s, font, scale) > w)
      s.pop_back();
    return s;
  }
  void Text(float x, float y, std::string_view text, std::uint32_t color,
            int font = 1, float scale = 1, bool center = false,
            bool shadow = true) {
    if (center)
      x -= Width(text, font, scale) / 2;
    if (shadow)
      Text(x + scale, y + scale, text, 0xd9000000, font, scale, false, false);
    const int heights[3] = {13, 16, 24};
    for (unsigned char c : text) {
      if (c < 32 || c >= 128)
        c = '?';
      int idx = c - 32;
      float w = widths[font][idx];
      if (c != ' ') {
        float u = (idx % 16) * 32.f + 1,
              v = font * 256.f + (idx / 16) * 40.f + 1;
        auto a = V(x - scale, y - scale, color, u / 512, v / 1024),
             b = V(x + (w + 1) * scale, y - scale, color, (u + w + 2) / 512,
                   v / 1024),
             d = V(x - scale, y + (heights[font] + 1) * scale, color, u / 512,
                   (v + heights[font] + 2) / 1024),
             e = V(x + (w + 1) * scale, y + (heights[font] + 1) * scale, color,
                   (u + w + 2) / 512, (v + heights[font] + 2) / 1024);
        Tri(a, b, d);
        Tri(b, e, d);
      }
      x += w * scale;
    }
  }
} canvas;
constexpr std::uint32_t TextColor = 0xffedf2fa, Accent = 0xff7ac7ff,
                        Danger = 0xffff4d40, Panel = 0x8f0e1117;
std::uint32_t Alpha(std::uint32_t color, float alpha) {
  return (color & 0xffffff) |
         (std::uint32_t(std::clamp(alpha, 0.f, 1.f) * 255) << 24);
}
struct Button {
  float x, y, w, h;
  int action;
};
std::vector<Button> buttons;
Button fullMapBounds{}, arrowSliderBounds{};
bool arrowSliding=false;
float menuX = 0, menuY = 0, menuW = 300, menuH = 0, mapCX = 0, mapCY = 0,
      mapR = 86;
bool Inside(float x, float y, const Button &b) {
  return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
}
bool MapHit(float x, float y) {
  return cfg.minimap && std::hypot(x - mapCX, y - mapCY) <= mapR;
}
bool OwnedPoint(float x, float y) {
  if (MapHit(x, y))
    return true;
  if ((fullMap || mapGesture.preview) && Inside(x, y, fullMapBounds))
    return true;
  if (menu && x >= menuX && x <= menuX + menuW && y >= menuY &&
      y <= menuY + menuH)
    return true;
  for (auto b : buttons)
    if (Inside(x, y, b))
      return true;
  return false;
}
void PublishExternalMap(const PlayerSnapshot& player,bool force=false);
void OpenHudExternalMap() {
  flushNativeMouse=true; // Do not replay the opening click when game focus returns.
  const auto* state=GetClientState();PlayerSnapshot player;
  if (state && state->GetPlayerSnapshot(player) && player.positionValid && player.headingValid)
    PublishExternalMap(player,true);
  if (!OpenExternalMap()) lastError="External map window could not be opened";
}
void UpdateArrowSlider(float x) {
  cfg.arrowScale=hud::ArrowScaleFromSlider(x,arrowSliderBounds.x,arrowSliderBounds.w);
}
void Action(int a) {
  auto names=GetNameplateOptions();
  auto tags=GetTagDisplayOptions();
  if(a>=4000 && a<4012 && padPicker){auto list=PadCommands();auto index=padPickerPage*12+(a-4000);if(index<int(list.size())){auto options=GetGamepadOptions();ReleaseGamepad(InGame());options.bindings[padInput]=list[index];SetGamepadOptions(options);padPicker=false;Save();}return;}
  if(a>=340 && a<=346){padCategory=a-340;padPickerPage=0;return;}

  if(a==140){menuPage=8;return;}
  if (a>=100 && a<108) { menuPage=a-100; return; }
  switch (a) {
  case 1:
    cfg.zoom = std::max(0, cfg.zoom - 1);
    break;
  case 2:
    cfg.zoom = std::min(4, cfg.zoom + 1);
    break;
  case 3:
    menu = !menu;if(!menu){CancelGamepadInputCapture();CancelGamepadKeyboardCapture();padListening=padKeyListening=padPicker=false;}
    break;
  case 4:
    cfg.minimap = !cfg.minimap;
    break;
  case 5:
    cfg.rotate = !cfg.rotate;
    break;
  case 6:
    cfg.autoZ = !cfg.autoZ;
    break;
  case 7:
    cfg.group = !cfg.group;
    break;
  case 8:
    cfg.floating = !cfg.floating;
    break;
  case 9:
    cfg.melee = !cfg.melee;
    break;
  case 10:
    cfg.spells = !cfg.spells;
    break;
  case 11:
    cfg.self = !cfg.self;
    break;
  case 12:
    cfg.others = !cfg.others;
    break;
  case 13:
    cfg.camera = !cfg.camera;
    UpdateZoom();
    break;
  case 14:
    cfg.fov = std::max(10.f, cfg.fov - 5);
    break;
  case 15:
    cfg.fov = std::min(100.f, cfg.fov + 5);
    break;
  case 16:
    cfg.maxZoom = std::max(5.f, cfg.maxZoom - 10);
    UpdateZoom();
    break;
  case 17:
    cfg.maxZoom = std::min(200.f, cfg.maxZoom + 10);
    UpdateZoom();
    break;
  case 18:
    cfg.above = std::min(1000, cfg.above + 10);
    break;
  case 19:
    cfg.above = std::max(1, cfg.above - 10);
    break;
  case 20:
    mapZone.clear();
    break;
  case 21:
    cfg.labels = !cfg.labels;
    break;
  case 22:
    cfg.bigHit = std::max(1, cfg.bigHit - 100);
    break;
  case 23:
    cfg.bigHit = std::min(100000000, cfg.bigHit + 100);
    break;
  case 24:
    cfg.smoothing = std::max(0.f, cfg.smoothing - .02f);
    break;
  case 25:
    cfg.smoothing = std::min(.5f, cfg.smoothing + .02f);
    break;
  case 26:
    cfg.below = std::max(1, cfg.below - 10);
    break;
  case 27:
    cfg.below = std::min(1000, cfg.below + 10);
    break;
  case 41: cfg.healing=!cfg.healing; break;
  case 42: cfg.players=!cfg.players; break;
  case 43: cfg.pets=!cfg.pets; break;
  case 44: cfg.hpBars=!cfg.hpBars; break;
  case 45: cfg.manaBars=!cfg.manaBars; break;
  case 46: cfg.staminaBars=!cfg.staminaBars; break;
  case 47: cfg.barPlayers=!cfg.barPlayers; break;
  case 48: cfg.barNpcs=!cfg.barNpcs; break;
  case 49: cfg.barPets=!cfg.barPets; break;
  case 50: cfg.barSelf=!cfg.barSelf; break;
  case 51: cfg.barsTargetOnly=!cfg.barsTargetOnly; break;
  case 52: cfg.barRange=std::max(25.f,cfg.barRange-25); break;
  case 53: cfg.barRange=std::min(1000.f,cfg.barRange+25); break;
  case 54: names.conColors=!names.conColors; break;
  case 55: names.pinkTarget=!names.pinkTarget; break;
  case 56: names.brackets=!names.brackets; break;
  case 57: names.attackFlash=!names.attackFlash; break;
  case 58: names.flashChangesPerSecond=std::max(1.f,names.flashChangesPerSecond-.5f); break;
  case 59: names.flashChangesPerSecond=std::min(16.f,names.flashChangesPerSecond+.5f); break;
  case 60: tags.enabled=!tags.enabled; break;
  case 61: tags.models=!tags.models; break;
  case 62: tags.text=!tags.text; break;
  case 63: tags.defaultArrow=!tags.defaultArrow; break;
  case 64: tags.filter=!tags.filter; break;
  case 65: tags.suppress=!tags.suppress; break;
  case 66: tags.prettyprint=!tags.prettyprint; break;
  case 67: RequestClearTags(); break;
  case 68: cfg.scale=std::max(.5f,cfg.scale-.1f); break;
  case 69: cfg.scale=std::min(3.f,cfg.scale+.1f); break;
  case 76:cfg.life.npc=!cfg.life.npc;break;
  case 77:cfg.life.player=!cfg.life.player;break;
  case 78:cfg.life.feign=!cfg.life.feign;break;
  case 79:cfg.life.laugh=!cfg.life.laugh;break;
  case 80:cfg.life.duration=std::max(4.f,cfg.life.duration-1);break;
  case 81:cfg.life.duration=std::min(20.f,cfg.life.duration+1);break;
  case 82:cfg.life.rise=std::max(.25f,cfg.life.rise-.1f);break;
  case 83:cfg.life.rise=std::min(3.f,cfg.life.rise+.1f);break;
  case 84:cfg.life.npcScale=std::max(.5f,cfg.life.npcScale-.1f);break;
  case 85:cfg.life.npcScale=std::min(4.f,cfg.life.npcScale+.1f);break;
  case 86:cfg.life.playerScale=std::max(1.f,cfg.life.playerScale-.1f);break;
  case 87:cfg.life.playerScale=std::min(6.f,cfg.life.playerScale+.1f);break;
  case 88:cfg.life.jesterScale=std::max(.5f,cfg.life.jesterScale-.1f);break;
  case 89:cfg.life.jesterScale=std::min(5.f,cfg.life.jesterScale+.1f);break;
  case 92:case 93:cfg.life.npcColor=ghostPalette[(GhostColorIndex(cfg.life.npcColor)+(a==92?7:1))%8];break;
  case 94:case 95:cfg.life.playerColor=ghostPalette[(GhostColorIndex(cfg.life.playerColor)+(a==94?7:1))%8];break;
  case 108:case 109:case 110:case 111:case 112:case 113:case 114:case 115:case 116:case 117:case 118:case 119:{
    auto options=GetGamepadOptions();
    if(a==108)options.enabled=!options.enabled;
    if(a==109 || a==110)options.slot=(options.slot+(a==109?15:1))%16;
    if(a==111 || a==112)options.deadzone=std::clamp(options.deadzone+(a==111?-.05f:.05f),.1f,.85f);
    if(a==113 || a==114)padInput=(padInput+(a==113?pad::Count-1:1))%pad::Count;
    if(a==115 || a==116)options.bindings[padInput]=CycleGamepadCommand(options.bindings[padInput],a==115?-1:1);
    if(a==117)options.bindings[padInput]=-1;
    if(a==118){ReleaseGamepad(InGame());options.bindings=pad::Options{}.bindings;}
    if(a==119)ReleaseGamepad(InGame());
    SetGamepadOptions(options);break;
  }
  case 120:case 121:case 122:case 123:case 124:{
    auto options=GetGamepadOptions();
    if(a==120 || a==121)options.mouseStick=(options.mouseStick+(a==120?2:1))%3;
    if(a==122 || a==123)options.mouseSpeed=std::clamp(options.mouseSpeed+(a==122?-100.f:100.f),100.f,2400.f);
    if(a==124)options.invertMouseY=!options.invertMouseY;
    ReleaseGamepad(InGame());SetGamepadOptions(options);break;
  }
  case 130:case 131:case 132:case 133:case 134:{
    auto options=GetPipeOptions();if(a==130)options.enabled=!options.enabled;if(a==131)options.verbose=!options.verbose;if(a==134)options.zealAlias=!options.zealAlias;
    if(a==132 || a==133)options.delay=std::clamp(options.delay+(a==132?-25:25),25,5000);
    SetPipeOptions(options);break;
  }
  case 144:case 145:case 146:case 147:case 148:case 149:{
    auto options=GetGamepadOptions();
    if(a==144 || a==145)options.backend=(options.backend+(a==144?2:1))%3;
    if(a==146 || a==147)options.joystickLayout=(options.joystickLayout+(a==146?2:1))%3;
    if((a==148 || a==149) && padInput<16){auto& b=options.joystickButtons[padInput];b+=a==148?-1:1;if(b>31)b=-1;if(b< -1)b=31;}
    ReleaseGamepad(InGame());SetGamepadOptions(options);break;
  }
  case 210:case 211:barEdit=(barEdit+(a==210?3:1))%4;break;
  case 213:case 214:cfg.npcHpColor=barColors[(std::max(0,BarColorIndex(cfg.npcHpColor))+(a==213?8:1))%9];break;
  case 212:cfg.barRpg=!cfg.barRpg;break;
  case 300:padPicker=true;padPickerPage=0;padCategory=1;padModifiers=GetGamepadOptions().bindings[padInput]<pad::MouseLeft?std::max(0,GetGamepadOptions().bindings[padInput])&0x700:0;break;
  case 386:case 387:case 388:padModifiers^=a==386?pad::Control:a==387?pad::Alt:pad::Shift;padPickerPage=0;break;
  case 301:padPicker=false;break;
  case 302:padPickerPage=std::max(0,padPickerPage-1);break;
  case 303:padPickerPage=std::min(int(PadCommands().size()+11)/12-1,padPickerPage+1);break;
  case 310:CancelGamepadKeyboardCapture();padListening=true;padKeyListening=false;padBindingFeedback.clear();BeginGamepadInputCapture();break;
  case 311:CancelGamepadInputCapture();CancelGamepadKeyboardCapture();padListening=padKeyListening=false;padBindingFeedback="Mapping cancelled; old binding kept.";break;
  case 330:padDevicePage=true;padPicker=false;break;
  case 331:padDevicePage=false;break;
  case 320:case 321:case 322:case 323:{
    auto options=GetGamepadOptions();ReleaseGamepad(InGame());options.bindings[padInput]=a==323?-1:pad::MouseLeft+(a-320);SetGamepadOptions(options);break;
  }
  case 200:case 201:cfg.barWidths[barEdit]=std::clamp(cfg.barWidths[barEdit]+(a==200?-4.f:4.f),24.f,160.f);break;
  case 202:case 203:cfg.barHeights[barEdit]=std::clamp(cfg.barHeights[barEdit]+(a==202?-1.f:1.f),2.f,16.f);break;
  case 204:case 205:case 206:case 207:case 208:case 209:{
    auto& color=a<206?cfg.hpColor:a<208?cfg.manaColor:cfg.staminaColor;
    color=barColors[(std::max(0,BarColorIndex(color))+(a%2==0?8:1))%9];break;
  }
  case 90:cfg.life.range=std::max(25.f,cfg.life.range-25);break;
  case 91:cfg.life.range=std::min(1000.f,cfg.life.range+25);break;
  case 70: fullMap=!fullMap; break;
  case 74: OpenHudExternalMap(); break;
  case 73: cfg.petOwnerNames=!cfg.petOwnerNames; SetNativePetOwnerNames(cfg.petOwnerNames); break;
  case 71: case 72: {
    int type=0; const auto address=VA(0xd1fd9c);
    if (address && Read(reinterpret_cast<void*>(address),0,type)) {
      type=(std::clamp(type,0,4)+(a==71?4:1))%5;
      SIZE_T written; WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&type,4,&written);
    }
    break;
  }
  case 40:
    fullMap = false;
    break;
  default:
    if (a >= 30 && a < 34)
      cfg.layers ^= 1 << (a - 30);
    break;
  }
  if (a>=54 && a<=59) SetNameplateOptions(names);
  if (a>=60 && a<=66) SetTagDisplayOptions(tags);
  Save();
}
void CancelHudInput(UINT message) {
  // Losing mouse capture is independent of keyboard focus. EQ can transfer
  // capture during ordinary play without releasing a held movement key.
  if(message!=WM_CAPTURECHANGED){
    ReleaseGamepad(InGame());CancelGamepadKeyboardCapture();CancelGamepadInputCapture();padListening=padKeyListening=false;
  }
  if (arrowSliding) {arrowSliding=false;Save();}
  mapGesture.Cancel();
  dragging = ownedLeft = ownedRight = false;
}
LRESULT CALLBACK HudWndProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  if (message == WM_NCDESTROY) {
    auto previous = previousWndProc;
    window = nullptr;
    previousWndProc = nullptr;
    return previous ? CallWindowProcW(previous, hwnd, message, wp, lp)
                    : DefWindowProcW(hwnd, message, wp, lp);
  }
  bool consumed = false, arrowCursor = false;
  int capture = 0;
  if (InGame()) {
    Guard guard;
    POINT p{};
    GetCursorPos(&p);
    ScreenToClient(hwnd, &p);
    mouse = p;
    float x = float(p.x), y = float(p.y);
    const bool mappingMouse=GamepadKeyboardCaptureBlocking() &&
      (message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_LBUTTONDBLCLK||
       message==WM_RBUTTONDOWN||message==WM_RBUTTONUP||message==WM_RBUTTONDBLCLK||
       message==WM_MBUTTONDOWN||message==WM_MBUTTONUP||message==WM_MBUTTONDBLCLK);
    if(mappingMouse){
      const int key=(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_LBUTTONDBLCLK)?VK_LBUTTON:
        (message==WM_RBUTTONDOWN||message==WM_RBUTTONUP||message==WM_RBUTTONDBLCLK)?VK_RBUTTON:VK_MBUTTON;
      FeedGamepadKeyboardCapture(key,message!=WM_LBUTTONUP&&message!=WM_RBUTTONUP&&message!=WM_MBUTTONUP);consumed=true;
    } else if(GamepadKeyboardCaptureBlocking() && (message==WM_KEYDOWN||message==WM_SYSKEYDOWN||message==WM_KEYUP||message==WM_SYSKEYUP)){
      FeedGamepadKeyboardCapture(int(wp),message==WM_KEYDOWN||message==WM_SYSKEYDOWN);consumed=true;
    } else if(GamepadKeyboardCaptureBlocking() && (message==WM_CHAR||message==WM_SYSCHAR)){
      consumed=true;
    } else if (message == WM_SETCURSOR && LOWORD(lp)==HTCLIENT && OwnedPoint(x,y)) {
      arrowCursor=true;
      consumed=true;
    } else if (message == WM_MOUSEWHEEL) {
      if (OwnedPoint(x, y)) {
        wheelMessageTick = GetTickCount64();
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (delta && MapHit(x, y)) {
          cfg.zoom = std::clamp(cfg.zoom + (delta > 0 ? -1 : 1), 0, 4);
          Save();
        }
        consumed = true;
      }
    } else if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) && OwnedPoint(x, y)) {
      ownedLeft = true;
      blockLeftUntil = GetTickCount64() + 250;
      consumed = true;
      bool button = false;
      for (auto b : buttons)
        if (Inside(x, y, b)) {
          if (b.action==75) { arrowSliding=true; UpdateArrowSlider(x); }
          else Action(b.action);
          button = true;
          break;
        }
      if (!button && MapHit(x, y)) {
        dragging=mapGesture.Press(x,y,GetTickCount64(),GetDoubleClickTime(),
          float(GetSystemMetrics(SM_CXDOUBLECLK))/2,
          float(GetSystemMetrics(SM_CYDOUBLECLK))/2);
        dragX=x-cfg.x; dragY=y-cfg.y;
        if (dragging) capture=1;
      } else mapGesture.Cancel();
    } else if (message == WM_MOUSEMOVE && arrowSliding && ownedLeft) {
      UpdateArrowSlider(x); consumed=true;
    } else if (message == WM_MOUSEMOVE && dragging) {
      cfg.x = std::max(0.f,x-dragX);
      cfg.y = std::max(0.f,y-dragY);
      consumed = true;
    } else if (message == WM_LBUTTONUP && ownedLeft) {
      if (arrowSliding) { arrowSliding=false; Save(); }
      const bool wasDragging=dragging;
      mapGesture.Release(GetTickCount64());
      dragging=false;
      if (wasDragging) { capture=-1; Save(); }
      ownedLeft = false;
      blockLeftUntil = GetTickCount64() + 250;
      consumed = true;
    } else if (message == WM_RBUTTONDOWN && OwnedPoint(x, y)) {
      ownedRight = true;
      blockRightUntil = GetTickCount64() + 250;
      consumed = true;
      if (GetKeyState(VK_MENU) < 0 || !MapHit(x, y))
        menu = !menu;
      else
        cfg.rotate = !cfg.rotate;
      Save();
    } else if (message == WM_RBUTTONUP && ownedRight) {
      ownedRight = false;
      blockRightUntil = GetTickCount64() + 250;
      consumed = true;
    } else if(message==WM_KEYDOWN && wp==VK_ESCAPE && menu && padPicker){
      padPicker=false;consumed=true;
    } else if (message == WM_KEYDOWN && wp == VK_ESCAPE && (menu || fullMap)) {
      menu = false;CancelGamepadInputCapture();CancelGamepadKeyboardCapture();padListening=padKeyListening=padPicker=false;
      fullMap = false;
      mapGesture.Cancel();
      dragging=false; capture=-1;
      consumed = true;
    } else if (message == WM_CAPTURECHANGED || message == WM_KILLFOCUS || message == WM_CANCELMODE) {
      CancelHudInput(message);
      if (message != WM_CAPTURECHANGED) capture=-1;
    }
  }
  if (capture > 0)
    SetCapture(hwnd);
  else if (capture < 0 && GetCapture() == hwnd)
    ReleaseCapture();
  if (arrowCursor) {
    SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));
    return TRUE;
  }
  if (consumed)
    return 0;
  return previousWndProc
             ? CallWindowProcW(previousWndProc, hwnd, message, wp, lp)
             : DefWindowProcW(hwnd, message, wp, lp);
}
using GetSpawnFn = void *(__thiscall *)(void *, int);
void *Spawn(std::uint32_t id) {
  if (!id || id > INT_MAX)
    return nullptr;
  auto manager = Global(SpawnManager, 0x18);
  auto fn = VA(0x5996e0, 5);
  return manager && fn ? reinterpret_cast<GetSpawnFn>(fn)(manager, int(id))
                       : nullptr;
}
bool WorldPosition(void *spawn, Point &p, float &height) {
  void *actor = nullptr;
  if (!Read(spawn, 0x101c, actor) || !actor || !Read(actor, 0x114, p) ||
      !Read(spawn, 0x13c, height))
    return false;
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
         std::isfinite(height) && height >= 0 && height < 1000;
}
std::unordered_set<std::uint32_t> GroupPlayerIds() {
  std::unordered_set<std::uint32_t> ids;
  void *group=nullptr;
  if (Read(Global(LocalPC),0x31cc,group) && group)
    for (int i=0;i<6;++i) {
      void *member=nullptr,*spawn=nullptr; std::uint32_t id=0;
      if (Read(group,4+i*4,member)&&Read(member,0x28,spawn)&&Read(spawn,0x148,id)&&id) ids.insert(id);
    }
  return ids;
}
bool ProfileResource(void *pc,std::size_t offset,int& value) {
  void *vbtable=nullptr,*list=nullptr;
  int displacement=0,type=0;
  if (!Read(pc,8,vbtable)||!Read(vbtable,4,displacement)||displacement<0||displacement>0x10000)
    return false;
  const std::size_t manager=std::size_t(displacement)+12;
  if (!Read(pc,manager,list)||!Read(pc,manager+4,type)) return false;
  std::unordered_set<void*> seen;
  while (list && seen.size()<64 && seen.insert(list).second) {
    int kind=-1; void *profile=nullptr,*next=nullptr;
    if (!Read(list,0,kind)||!Read(list,4,profile)||!Read(list,12,next)) return false;
    if (kind==type) return Read(profile,offset,value) && value>=0;
    list=next;
  }
  return false;
}
bool ProfileMana(void *pc,int& mana) { return ProfileResource(pc,0x338c,mana); }
bool LocalMana(int& current,int& maximum) {
  void *pc=Global(LocalPC);
  const auto fn=VA(0x581e60,9);
  constexpr BYTE expected[]={0x56,0x8b,0xf1,0x8b,0x86,0x40,0xd2,0xff,0xff};
  std::array<BYTE,9> actual{};
  if (!pc || !ProfileMana(pc,current) || !fn ||
      !Read(reinterpret_cast<void*>(fn),0,actual) || std::memcmp(actual.data(),expected,9)) return false;
  using MaxManaFn=int(__thiscall*)(void*,bool);
  maximum=reinterpret_cast<MaxManaFn>(fn)(static_cast<BYTE*>(pc)+0x2dc8,true);
  return maximum>0;
}
bool LocalEndurance(int& current,int& maximum) {
  void *pc=Global(LocalPC);
  const auto fn=VA(0x582020,8);
  constexpr BYTE expected[]={0x83,0xec,0x08,0x53,0x55,0x56,0x8b,0xf1};
  std::array<BYTE,8> actual{};
  if (!pc || !ProfileResource(pc,0x3390,current) || !fn ||
      !Read(reinterpret_cast<void*>(fn),0,actual) || std::memcmp(actual.data(),expected,8)) return false;
  using MaxEnduranceFn=int(__thiscall*)(void*,bool);
  maximum=reinterpret_cast<MaxEnduranceFn>(fn)(static_cast<BYTE*>(pc)+0x2dc8,true);
  return maximum>0;
}
void LoadSkeletonLaugh() {
  skeletonLaughId=-1;
  wchar_t path[32768]{};if(!GetModuleFileNameW(game,path,32768))return;
  const auto file=std::filesystem::path(path).parent_path()/L"soundassets.txt";
  std::error_code ec;if(std::filesystem::file_size(file,ec)>4*1024*1024 || ec)return;
  std::ifstream input(file);std::string line;
  while(std::getline(input,line)) {
    const auto split=line.find('^');if(split==line.npos)continue;
    int id=-1;auto parsed=std::from_chars(line.data(),line.data()+split,id);
    if(parsed.ec!=std::errc{} || parsed.ptr!=line.data()+split || id<0 || id>2048)continue;
    auto name=line.substr(split+1);name=name.substr(0,name.find_first_of("^\r\n"));
    for(auto& c:name)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    if(name=="skelatt1.wav"){skeletonLaughId=id;return;}
  }
}
void PlaySkeletonLaugh() {
  if(skeletonLaughId<0)return;
  constexpr BYTE expected[]={0x83,0xec,0x30,0x56,0x8b,0xf1};
  const auto address=VA(0x4a9140,sizeof(expected));std::array<BYTE,sizeof(expected)> bytes{};
  void* manager=Global({0xd20460,addresses::Storage::PointerStorage});void* asset=nullptr;
  if(!manager || !address || !Read(reinterpret_cast<void*>(address),0,bytes) ||
      std::memcmp(bytes.data(),expected,sizeof(expected)) ||
      !Read(manager,0x1004+std::size_t(skeletonLaughId)*8,asset) || !asset) {
    lastError="Skeleton laugh sound is unavailable";return;
  }
  using WaveFn=void(__thiscall*)(void*,int,void*);
  reinterpret_cast<WaveFn>(address)(manager,skeletonLaughId,nullptr);
}
void CaptureLifeSample(void* spawn,const std::array<BYTE,0x498>& data,ULONGLONG now) {
  if(!InGame())return;
  life::Sample sample;
  auto field=[&]<class T>(std::size_t offset,T& value){std::memcpy(&value,data.data()+offset,sizeof(value));};
  field(0x148,sample.id);field(0x125,sample.type);field(0x35c,sample.stand);
  field(0x2e4,sample.hp);field(0x2dc,sample.maximum);sample.identity=reinterpret_cast<std::uintptr_t>(spawn);
  Point position;field(0x68,position.x);field(0x64,position.y);field(0x6c,position.z);
  const bool nearby=lifeViewerValid && std::isfinite(position.x)&&std::isfinite(position.y)&&std::isfinite(position.z) &&
    std::hypot(std::hypot(position.x-lifeViewer.x,position.y-lifeViewer.y),position.z-lifeViewer.z)<=cfg.life.range;
  if(nearby && (cfg.life.npc||cfg.life.player||cfg.life.feign))
    sample.positionValid=WorldPosition(spawn,sample.position,sample.height);
  if(ObserveLifeSample(sample,now,nearby))PlaySkeletonLaugh();
}
void RefreshHudSpawns(ULONGLONG now) {
  if (spawnTick && now-spawnTick<100) return;
  spawnTick=now;
  hudSpawns.clear();
  std::unordered_set<void*> seen;
  std::unordered_map<std::uint32_t,hud::HealthSample> health;
  void *spawn=nullptr;
  // PlayerManagerClient adds a vptr before PlayerManagerBase; TList starts +8.
  Read(Global(SpawnManager),8,spawn);
  while (spawn && seen.size()<4096 && seen.insert(spawn).second) {
    HudSpawn e; e.object=spawn;
    // One checked copy avoids a separate process-memory call for every field.
    std::array<BYTE,0x498> bytes{};
    if (!Read(spawn,0,bytes)) break;
    CaptureLifeSample(spawn,bytes,now);
    auto field=[&]<class T>(std::size_t offset,T& out) {
      static_assert(sizeof(T)<=0x498);
      if (offset<=bytes.size()-sizeof(T)) std::memcpy(&out,bytes.data()+offset,sizeof(T));
    };
    void *next=nullptr; field(8,next); // vptr-adjusted TListNode::m_pNext.
    field(0x148,e.id); field(0x125,e.type);
    field(0x68,e.position.x); field(0x64,e.position.y); field(0x6c,e.position.z);
    field(0x80,e.heading); field(0x38c,e.owner);
    field(0x2e4,e.hp); field(0x2dc,e.hpMax);
    field(0x494,e.mana); field(0x354,e.manaMax);
    field(0x1a0,e.stamina); field(0x3c4,e.staminaMax);
    if (e.id && e.type<=1 && std::isfinite(e.position.x) &&
        std::isfinite(e.position.y) && std::isfinite(e.position.z) && std::isfinite(e.heading)) {
      if (e.id==localId) {
        // Spawn mana fields are not the authoritative local-character source.
        e.mana=-1; e.manaMax=0;
        LocalMana(e.mana,e.manaMax);
        e.stamina=-1; e.staminaMax=0; LocalEndurance(e.stamina,e.staminaMax);
        localManaCurrent=e.mana; localManaMaximum=e.manaMax;
      }
      hud::HealthSample h{reinterpret_cast<std::uintptr_t>(spawn),e.hp,e.hpMax,
        e.type==0,e.owner!=0,e.id==localId};
      auto old=previousHealth.find(e.id);
      if (cfg.floating && cfg.healing && old!=previousHealth.end()) {
        auto gain=hud::DetectHealing(old->second,h);
        if (gain.amount>0) {
          hud::FloatEffect effect;
          effect.target=e.id; effect.identity=h.identity;
          effect.damage=gain.amount; effect.healing=true; effect.percent=gain.percent;
          effect.born=now; effect.x=float(int((now+e.id*17)%61)-30);
          if (effects.size()>=256) effects.erase(effects.begin());
          effects.push_back(effect);
        }
      }
      health.emplace(e.id,h);
      hudSpawns.push_back(e);
    }
    spawn=next;
  }
  previousHealth=std::move(health);
}
bool ScreenHead(void *spawn,const D3DMATRIX& vp,const D3DVIEWPORT9& view,float &x,float &y,float* depth=nullptr) {
  Point p; float height=0;
  if (!WorldPosition(spawn,p,height)) return false;
  p.z+=height+1.5f;
  float clip[4]{};
  for (int c=0;c<4;++c)
    clip[c]=p.x*vp.m[0][c]+p.y*vp.m[1][c]+p.z*vp.m[2][c]+vp.m[3][c];
  if (!std::isfinite(clip[3]) || clip[3]<=.01f) return false;
  const float nx=clip[0]/clip[3], ny=clip[1]/clip[3], nz=clip[2]/clip[3];
  if (!std::isfinite(nx)||!std::isfinite(ny)||!std::isfinite(nz)||nz<0||nz>1||std::fabs(nx)>1||std::fabs(ny)>1) return false;
  x=view.X+(nx+1)*view.Width/2; y=view.Y+(1-ny)*view.Height/2;
  if(depth)*depth=view.MinZ+nz*(view.MaxZ-view.MinZ);
  return true;
}
void DrawOverheadBars(void *render,const D3DVIEWPORT9& view,const PlayerSnapshot& player,int pass=0) {
  if (!cfg.hpBars && !cfg.manaBars && !cfg.staminaBars) return;
  D3DMATRIX vp{};
  if (!Read(render,0x16c0,vp)) return;
  auto state=GetClientState();
  void *target=state?state->GetTarget():nullptr;
  const auto grouped=GroupPlayerIds();
  const float s=cfg.scale;
  for (const auto &e:hudSpawns) {
    if((pass==1 && e.type==0)||(pass==2 && e.type!=0))continue;
    if ((e.id==localId && !cfg.barSelf) ||
        (e.owner ? !cfg.barPets : e.type==0 ? !cfg.barPlayers : !cfg.barNpcs) ||
        (cfg.barsTargetOnly && e.object!=target) ||
        std::hypot(std::hypot(e.position.x-player.x,e.position.y-player.y),e.position.z-player.z)>cfg.barRange)
      continue;
    std::uint32_t actual=0;
    float x=0,y=0,depth=0;
    if (!Read(e.object,0x148,actual)||actual!=e.id||!ScreenHead(e.object,vp,view,x,y,&depth)) continue;
    canvas.Depth(e.type!=0,depth);
    const bool allowMana=cfg.manaBars && hud::ShowManaBar(e.type==0,e.id==localId,grouped.contains(e.id));
    const bool allowStamina=cfg.staminaBars && hud::ShowStaminaBar(e.type==0);
    float stack=0;int rows=0;float gap=cfg.barRpg?7.f:3.f;
    auto account=[&](int resource,bool enabled,int current,int maximum){if(enabled && hud::ResourceRatio(current,maximum)>=0){stack+=cfg.barHeights[resource]+gap;++rows;}};
    const int hpResource=e.type==0?0:3;
    account(hpResource,cfg.hpBars,e.hp,e.hpMax);account(1,allowMana,e.mana,e.manaMax);account(2,allowStamina,e.stamina,e.staminaMax);
    if(rows)stack-=gap;y-=std::max(36.f,stack+2)*s;
    auto bar=[&](int resource,bool enabled,int value,int maximum,std::uint32_t color) {
      const float w=cfg.barWidths[resource]*s,h=cfg.barHeights[resource]*s;
      const float ratio=hud::ResourceRatio(value,maximum);
      if (!enabled||ratio<0) return;
      if(cfg.barRpg){
        canvas.Rect(x-w/2-3*s,y-3*s,w+6*s,h+6*s,0xe8000000);
        canvas.Rect(x-w/2-2*s,y-2*s,w+4*s,h+4*s,0xff5b4329);
        canvas.Line(x-w/2-2*s,y-2*s,x+w/2+2*s,y-2*s,0xffc6a468,s);
        canvas.Line(x-w/2-2*s,y+h+2*s,x+w/2+2*s,y+h+2*s,0xff32271e,s);
        for(int side:{-1,1}){float xx=x+side*(w/2+3*s);canvas.Line(xx-2*s,y+h/2,xx,y-2*s,0xffad8a52,s);canvas.Line(xx,y-2*s,xx+2*s,y+h/2,0xffad8a52,s);canvas.Line(xx-2*s,y+h/2,xx,y+h+2*s,0xff6d5031,s);canvas.Line(xx,y+h+2*s,xx+2*s,y+h/2,0xff6d5031,s);}
      }else canvas.Rect(x-w/2-s,y-s,w+2*s,h+2*s,0xe6000000);
      canvas.Rect(x-w/2,y,w,h,0xd92e394c);
      if (ratio>0) canvas.Rect(x-w/2,y,w*ratio,h,color);
      if(cfg.barRpg && ratio>0){canvas.Rect(x-w/2,y,w*ratio,std::min(s,h*.2f),0x40ffffff);canvas.Rect(x-w/2,y+h-s,w*ratio,s,0x55000000);}
      y+=h+(cfg.barRpg?7:3)*s;
    };
    bar(hpResource,cfg.hpBars,e.hp,e.hpMax,e.type==0?cfg.hpColor:cfg.npcHpColor);
    bar(1,cfg.manaBars && hud::ShowManaBar(e.type==0,e.id==localId,grouped.contains(e.id)),e.mana,e.manaMax,cfg.manaColor);
    bar(2,cfg.staminaBars && hud::ShowStaminaBar(e.type==0),e.stamina,e.staminaMax,cfg.staminaColor);
  }
  canvas.Depth(false);
}
void LoadMap(const std::string &name) {
  map = {};
  externalMapData.reset();
  mapZone = name;
  if (!hud::ValidZoneName(name))
    return;
  wchar_t path[32768]{};
  DWORD size = GetModuleFileNameW(game, path, 32768);
  if (!size || size >= 32768)
    return;
  auto dir = std::filesystem::path(path).parent_path() / L"maps";
  for (int layer = 0; layer < 4; ++layer) {
    auto file =
        dir / (name + (layer ? "_" + std::to_string(layer) : "") + ".txt");
    std::error_code ec;
    auto bytes = std::filesystem::file_size(file, ec);
    if (ec || bytes > 16 * 1024 * 1024)
      continue;
    std::ifstream in(file, std::ios::binary);
    if (!in)
      continue;
    std::string data(std::istreambuf_iterator<char>(in), {});
    auto parsed = hud::ParseMap(data, layer);
    map.lines.insert(map.lines.end(), parsed.lines.begin(), parsed.lines.end());
    map.labels.insert(map.labels.end(), parsed.labels.begin(),
                      parsed.labels.end());
  }
  externalMapData=std::make_shared<const hud::Map>(map);
  if (map.lines.empty())
    lastError =
        "No map lines: install zone .txt files in the client's maps folder";
}
void PublishExternalMap(const PlayerSnapshot& player,bool force) {
  if (!force && !ExternalMapWindow()) return;
  static ULONGLONG published=0;
  const auto now=GetTickCount64(); if(!force && now-published<100)return; published=now;
  ExternalMapState out;
  out.map=externalMapData; out.zone=zoneName;
  out.player={player.x,player.y,player.z}; out.heading=player.heading;
  out.arrowScale=cfg.arrowScale;out.layers=cfg.layers;out.labels=cfg.labels;
  out.autoZ=cfg.autoZ;out.above=float(cfg.above);out.below=float(cfg.below);
  auto group=GroupPlayerIds();
  for (const auto& e:hudSpawns) {
    if (e.id==localId) continue;
    const bool pet=e.owner!=0;
    if (pet?!cfg.pets:(e.type!=0 || (!cfg.players && !(cfg.group && group.contains(e.id))))) continue;
    out.markers.push_back({{-e.position.x,-e.position.y,e.position.z},e.heading,
      pet?0xffc9a477u:hud::PlayerArrowColor(e.id)});
  }
  UpdateExternalMap(std::move(out));
}
void DrawZone(float cx, float cy, float radius, float range,
              const PlayerSnapshot &player, bool rotating, bool circle,
              float halfW = 0, float halfH = 0) {
  const Point me{player.x, player.y, player.z};
  auto project = [&](Point p) {
    return hud::ProjectMap(p, me, player.heading, radius, range, rotating);
  };
  auto clip = [&](Point &a, Point &b) {
    return circle ? hud::ClipCircle(a, b, radius)
                  : hud::ClipRect(a, b, halfW, halfH);
  };
  auto pointInside = [&](Point p) {
    return circle ? std::hypot(p.x, p.y) < radius - 5
                  : std::fabs(p.x) < halfW - 5 && std::fabs(p.y) < halfH - 5;
  };
  for (const auto &l : map.lines) {
    if (!(cfg.layers & (1 << l.layer)))
      continue;
    Point a = project(l.a), b = project(l.b);
    if (!clip(a, b))
      continue;
    auto color = l.color;
    if ((color & 0xffffff) == 0)
      color = 0xffc6ccd6;
    float alpha = hud::HeightAlpha((l.a.z + l.b.z) / 2, player.z, cfg.autoZ,
                                   float(cfg.above), float(cfg.below));
    canvas.Line(cx + a.x, cy + a.y, cx + b.x, cy + b.y, Alpha(color, alpha),
                std::max(1.5f, radius / 90));
  }
  if (!circle && cfg.labels)
    for (const auto &l : map.labels) {
      if (!(cfg.layers & (1 << l.layer)))
        continue;
      auto p = project(l.p);
      if (!pointInside(p))
        continue;
      float alpha = hud::HeightAlpha(l.p.z, player.z, cfg.autoZ,
                                     float(cfg.above), float(cfg.below));
      canvas.Text(cx + p.x, cy + p.y, l.text,
                  Alpha((l.color & 0xffffff) ? l.color : TextColor, alpha), 0,
                  cfg.scale, true);
    }
  const auto groupIds=GroupPlayerIds();
  auto arrow=[&](Point p,float heading,std::uint32_t color,float size) {
    auto v=hud::HeadingArrow({cx+p.x,cy+p.y},heading-(rotating?player.heading:0),size);
    canvas.Tri(canvas.V(v[0].x,v[0].y,color),canvas.V(v[1].x,v[1].y,color),canvas.V(v[2].x,v[2].y,color));
  };
  for (const auto &e:hudSpawns) {
    if (e.id==localId) continue;
    const bool pet=e.owner!=0;
    if (pet ? !cfg.pets : (e.type!=0 || (!cfg.players && !(cfg.group && groupIds.contains(e.id))))) continue;
    auto p=project({-e.position.x,-e.position.y,e.position.z});
    if (!pointInside(p)) continue;
    auto color=pet ? 0xffc9a477u : hud::PlayerArrowColor(e.id);
    arrow(p,e.heading,color,7*cfg.scale*cfg.arrowScale);
  }
  arrow({0,0},player.heading,Accent,9*cfg.scale*cfg.arrowScale);
}
void RingButton(float x, float y, const char *text, int action) {
  float r = 12 * cfg.scale;
  bool hover = std::hypot(float(mouse.x) - x, float(mouse.y) - y) <= r;
  canvas.Circle(x, y, r, 0xf20e1117, true);
  canvas.Circle(x, y, r, Alpha(Accent, hover ? .9f : .35f), false,
                1.5f * cfg.scale);
  canvas.Text(x, y - 9 * cfg.scale, text, hover ? Accent : TextColor, 1,
              cfg.scale, true, false);
  buttons.push_back({x - r, y - r, r * 2, r * 2, action});
}
void DrawMinimap(const PlayerSnapshot &player, const D3DVIEWPORT9 &view) {
  float s = cfg.scale;
  if (cfg.x < 0)
    cfg.x = std::max(0.f, float(view.Width) - 220 * s);
  cfg.x = std::clamp(cfg.x, 0.f, std::max(0.f, float(view.Width) - 200 * s));
  cfg.y = std::clamp(cfg.y, 0.f, std::max(0.f, float(view.Height) - 248 * s));
  mapCX = cfg.x + 100 * s;
  mapCY = cfg.y + 116 * s;
  mapR = 86 * s;
  auto name = canvas.Fit(zoneName, 176 * s, 0, s);
  canvas.Text(mapCX, cfg.y + 4 * s, name, 0xfff7faff, 0, s, true);
  canvas.Circle(mapCX + s, mapCY + 3 * s, mapR + 2 * s, 0x32000000, true);
  canvas.Circle(mapCX, mapCY, mapR, Panel, true);
  canvas.Circle(mapCX, mapCY, mapR, 0x0effffff, false, 1);
  DrawZone(mapCX, mapCY, mapR, hud::MapRanges[cfg.zoom], player, cfg.rotate,
           true);
  canvas.Circle(mapCX, mapCY, mapR, Alpha(Accent, .35f), false, 1.5f * s);
  float a = cfg.rotate ? player.heading * 2 * hud::Pi / 512 : 0,
        nx = mapCX + std::sin(a) * mapR, ny = mapCY - std::cos(a) * mapR;
  canvas.Circle(nx, ny, 10 * s, 0xff0e1117, true);
  canvas.Circle(nx, ny, 10 * s, Alpha(Danger, .9f), false, 1.5f * s);
  canvas.Text(nx, ny - 6.5f * s, "N", Danger, 0, s, true, false);
  RingButton(mapCX + std::cos(35 * hud::Pi / 180) * (mapR + 2 * s),
             mapCY + std::sin(35 * hud::Pi / 180) * (mapR + 2 * s), "+", 1);
  RingButton(mapCX + std::cos(58 * hud::Pi / 180) * (mapR + 2 * s),
             mapCY + std::sin(58 * hud::Pi / 180) * (mapR + 2 * s), "-", 2);
  BYTE hour = 0, minute = 0;
  auto world = Global(WorldData, 8);
  if (Read(world, 4, hour) && Read(world, 5, minute) && hour >= 1 &&
      hour <= 24 && minute < 60) {
    int h = (int(hour) + 23) % 24;
    char text[32];
    std::snprintf(text, sizeof(text), "%d:%02u %s", h % 12 ? h % 12 : 12,
                  unsigned(minute), h < 12 ? "AM" : "PM");
    canvas.Text(mapCX, mapCY + mapR + 11 * s, text, 0xfff7faff, 0, s, true);
  }
  if (MapHit(float(mouse.x), float(mouse.y)) && !menu) {
    const float w = 290 * s, h = 62 * s;
    const float x = std::clamp(float(mouse.x) + 14 * s, 0.f,
                               std::max(0.f, float(view.Width) - w));
    const float y = std::clamp(float(mouse.y) + 18 * s, 0.f,
                               std::max(0.f, float(view.Height) - h));
    canvas.Rect(x, y, w, h, 0xf20e1117);
    canvas.Text(x + 8*s, y + 5*s, zoneName, TextColor, 0, s);
    canvas.Text(x + 8*s, y + 23*s, "Hold: map   Wheel: zoom (range " +
                std::to_string(int(hud::MapRanges[cfg.zoom])) + ")", TextColor, 0, s);
    canvas.Text(x + 8*s, y + 41*s, cfg.rotate ? "Right-click: north up" :
                "Right-click: turn with the player", TextColor, 0, s);
  }
}
void DrawFullMap(const PlayerSnapshot &player, const D3DVIEWPORT9 &view) {
  float s = cfg.scale, w = std::min(720.f * s, float(view.Width) - 40),
        h = std::min(580.f * s, float(view.Height) - 40),
        x = (view.Width - w) / 2, y = (view.Height - h) / 2;
  fullMapBounds = {x, y, w, h, 0};
  canvas.Rect(x, y, w, h, 0xf20d1015);
  canvas.Line(x, y, x + w, y, Accent, 2);
  canvas.Text(x + 16, y + 8, zoneName, TextColor, 1, s);
  canvas.Text(x + w - 20, y + 8, "X", Danger, 1, s);
  buttons.push_back({x + w - 40, y, 40, 32 * s, 40});
  DrawZone(x + w / 2, y + h / 2 + 12 * s, std::min(w, h) / 2 - 30 * s,
           hud::MapRanges[cfg.zoom] * 2, player, false, false, w / 2 - 14 * s,
           h / 2 - 42 * s);
  canvas.Text(x + w / 2, y + h - 24 * s,
              "Hold minimap: preview   Double-click and hold: move", 0xffa8b8cc, 0, s,
              true);
}
void DrawMenu(const D3DVIEWPORT9 &view) {
  const float s=std::min(cfg.scale,std::max(.5f,(float(view.Height)-20)/600.f));
  menuW=380*s; menuH=600*s;
  menuX=std::clamp(cfg.x-menuW-10*s,10.f,std::max(10.f,float(view.Width)-menuW-10));
  menuY=std::clamp(cfg.y,10.f,std::max(10.f,float(view.Height)-menuH-10));
  const float x=menuX; float y=menuY;
  canvas.Rect(x+3*s,y+4*s,menuW,menuH,0x66000000);
  canvas.Rect(x,y,menuW,menuH,0xfa12151c);
  canvas.Line(x,y,x+menuW,y,Accent,2*s);
  canvas.Text(x+14*s,y+10*s,"Gusto settings",Accent,1,s);
  canvas.Text(x+menuW-24*s,y+10*s,"X",Danger,1,s);
  buttons.push_back({x+menuW-36*s,y,36*s,36*s,3});
  y+=40*s;
  const char* pages[]={"Map","Combat","Bars","Nameplates","Camera","Tags","Death / FD","Gamepad","Pipes"};
  for (int i=0;i<9;++i) {
    float bx=x+10*s+(i%4)*90*s,by=y+(i/4)*29*s;
    canvas.Rect(bx,by,86*s,25*s,menuPage==i?0xff2b4059:0xff202936);
    canvas.Text(bx+6*s,by+5*s,pages[i],menuPage==i?Accent:TextColor,0,s);
    buttons.push_back({bx,by,86*s,25*s,i==8?140:100+i});
  }
  y+=100*s;
  auto row=[&](const char* label,bool on,int action) {
    canvas.Rect(x+14*s,y+3*s,13*s,13*s,on?Accent:0xff2e394c);
    canvas.Text(x+36*s,y,label,TextColor,0,s);
    buttons.push_back({x+10*s,y-2*s,menuW-20*s,22*s,action}); y+=22*s;
  };
  auto adjust=[&](std::string label,int minus,int plus) {
    canvas.Text(x+14*s,y,label,TextColor,0,s);
    canvas.Text(x+menuW-65*s,y,"-",Accent,1,s);
    canvas.Text(x+menuW-32*s,y,"+",Accent,1,s);
    buttons.push_back({x+menuW-80*s,y-2*s,30*s,21*s,minus});
    buttons.push_back({x+menuW-46*s,y-2*s,30*s,21*s,plus}); y+=23*s;
  };
  auto action=[&](const char* label,int id) {
    canvas.Text(x+14*s,y+3*s,label,Accent,0,s);
    buttons.push_back({x+10*s,y-2*s,menuW-20*s,24*s,id}); y+=25*s;
  };
  if (menuPage==0) {
    row("Show minimap",cfg.minimap,4); row("Rotate with player",cfg.rotate,5);
    row("Auto Z fade",cfg.autoZ,6); row("All player arrows",cfg.players,42);
    row("Pet arrows (light brown)",cfg.pets,43); row("Show group player arrows",cfg.group,7);
    row("Map labels (expanded map)",cfg.labels,21);
    for (int i=0;i<4;++i) { auto label="Layer "+std::to_string(i); row(label.c_str(),cfg.layers&(1<<i),30+i); }
    adjust("Auto Z above: "+std::to_string(cfg.above),19,18);
    adjust("Auto Z below: "+std::to_string(cfg.below),26,27);
    adjust("Map range: "+std::to_string(int(hud::MapRanges[cfg.zoom])),1,2);
    adjust("HUD scale (%): "+std::to_string(int(cfg.scale*100)),68,69);
    row("Expanded map",fullMap,70); action("Reload map files",20);
    canvas.Text(x+14*s,y,"Arrow size: "+std::to_string(int(cfg.arrowScale*100))+"%",TextColor,0,s);
    y+=18*s;
    arrowSliderBounds={x+20*s,y,menuW-40*s,12*s,75};
    canvas.Rect(arrowSliderBounds.x,y+4*s,arrowSliderBounds.w,4*s,0xff2e394c);
    float knob=arrowSliderBounds.x+(cfg.arrowScale-.5f)/2.5f*arrowSliderBounds.w;
    canvas.Rect(knob-4*s,y,8*s,12*s,Accent);
    buttons.push_back(arrowSliderBounds); y+=18*s;
    action("Open external map window",74);
  } else if (menuPage==1) {
    row("Floating combat text",cfg.floating,8); row("Healing / HP gains",cfg.healing,41);
    row("Melee / misses",cfg.melee,9); row("Spell damage",cfg.spells,10);
    row("Damage to me",cfg.self,11); row("Other people's damage",cfg.others,12);
    adjust("Big hit threshold: "+std::to_string(cfg.bigHit),22,23);
    canvas.Text(x+14*s,y+12*s,"Green healing uses received HP changes.",TextColor,0,s);
  } else if (menuPage==2) {
    row("Health bars",cfg.hpBars,44); row("Mana bars",cfg.manaBars,45);
    row("Stamina / endurance bars",cfg.staminaBars,46);
    row("Player bars",cfg.barPlayers,47); row("NPC bars",cfg.barNpcs,48);
    row("Pet bars",cfg.barPets,49); row("My own bars",cfg.barSelf,50);
    row("Target only",cfg.barsTargetOnly,51);
    adjust("Maximum distance: "+std::to_string(int(cfg.barRange)),52,53);
    const char* resources[]={"Player HP","Mana","Stamina","NPC HP"};
    adjust(std::string("Edit bar: ")+resources[barEdit],210,211);
    adjust("Selected width: "+std::to_string(int(cfg.barWidths[barEdit])),200,201);
    adjust("Selected height: "+std::to_string(int(cfg.barHeights[barEdit])),202,203);
    row("RPG bronze frames",cfg.barRpg,212);
    adjust("Player HP color: "+BarColorName(cfg.hpColor),204,205);
    adjust("NPC HP color: "+BarColorName(cfg.npcHpColor),213,214);
    adjust("Mana color: "+BarColorName(cfg.manaColor),206,207);
    adjust("Stamina color: "+BarColorName(cfg.staminaColor),208,209);
    canvas.Text(x+14*s,y+12*s,"NPCs: HP only. Mana: self / group.",TextColor,0,s);
    canvas.Text(x+14*s,y+30*s,"Unknown resource values are hidden.",TextColor,0,s);
  } else if (menuPage==3) {
    auto names=GetNameplateOptions();
    row("Pet owner names",cfg.petOwnerNames,73);
    row("NPC con colors",names.conColors,54); row("Pink NPC target",names.pinkTarget,55);
    row("Auto-attack target brackets",names.brackets,56);
    row("Pink / white auto-attack flash",names.attackFlash,57);
    adjust("Color changes / second: "+std::to_string(names.flashChangesPerSecond).substr(0,4),58,59);
  } else if (menuPage==4) {
    row("Camera enhancements",cfg.camera,13);
    adjust("FOV (Zeal units): "+std::to_string(int(cfg.fov)),14,15);
    adjust("Maximum zoom: "+std::to_string(int(cfg.maxZoom)),16,17);
    adjust("Smoothing (ms): "+std::to_string(int(cfg.smoothing*1000)),24,25);
    int mode=0; Read(reinterpret_cast<void*>(VA(0xd1fd9c)),0,mode);
    adjust("Native camera mode: "+std::to_string(mode),71,72);
  } else if(menuPage==5) {
    auto tags=GetTagDisplayOptions();
    row("Tagging enabled",tags.enabled,60); row("3D tag models",tags.models,61);
    row("Tag nameplate text",tags.text,62); row("Default arrow",tags.defaultArrow,63);
    row("Filter protocol chat",tags.filter,64); row("Suppress tag chat",tags.suppress,65);
    row("Pretty-print tag chat",tags.prettyprint,66);
    action("Clear received / local tags",67);
    canvas.Text(x+14*s,y+8*s,"Use /tag join Gt123 for shared channels.",TextColor,0,s);y+=38*s;
  } else if(menuPage==6) {
    row("NPC death ghost",cfg.life.npc,76);
    row("Player death ghost",cfg.life.player,77);
    row("Player feign death: blue jester",cfg.life.feign,78);
    row("Skeleton laugh on feign death",cfg.life.laugh,79);
    auto number=[](float value){return std::to_string(value).substr(0,4);};
    adjust("Lifetime (seconds): "+number(cfg.life.duration),80,81);
    adjust("Rise (units / second): "+number(cfg.life.rise),82,83);
    adjust("NPC ghost size: "+number(cfg.life.npcScale),84,85);
    adjust("Player ghost size: "+number(cfg.life.playerScale),86,87);
    adjust("Jester head size: "+number(cfg.life.jesterScale),88,89);
    adjust("Maximum distance: "+std::to_string(int(cfg.life.range)),90,91);
    adjust(std::string("NPC color: ")+ghostColorNames[GhostColorIndex(cfg.life.npcColor)],92,93);
    adjust(std::string("Player color: ")+ghostColorNames[GhostColorIndex(cfg.life.playerColor)],94,95);
  } else if(menuPage==7) {
    if(padListening){int captured=ConsumeGamepadInputCapture();if(captured>=0){padInput=captured;padListening=false;padKeyListening=true;BeginGamepadKeyboardCapture();}}
    if(padKeyListening){int key=ConsumeGamepadKeyboardCapture();if(key>=0){auto changed=GetGamepadOptions();changed.bindings[padInput]=key;SetGamepadOptions(changed);padKeyListening=false;padBindingFeedback="Saved: "+std::string(GamepadInputName(padInput))+" -> "+GamepadCommandName(key);Save();}else if(key==-3){padKeyListening=false;padBindingFeedback="Mapping cancelled; old binding kept.";}}
    auto options=GetGamepadOptions();
    if(padPicker){
      canvas.Text(x+14*s,y,std::string("Assign: ")+GamepadInputName(padInput),Accent,0,s);y+=25*s;
      const char* categories[]={"All","Letters","Digits","F-keys","Nav","NumPad","Mouse"};
      for(int i=0;i<7;++i){float xx=x+10*s+i*51*s;canvas.Rect(xx,y,49*s,24*s,i==padCategory?0xff2b4059:0xff202936);canvas.Text(xx+3*s,y+4*s,categories[i],TextColor,0,s);buttons.push_back({xx,y,49*s,24*s,340+i});}y+=30*s;
      const char* modifiers[]={"Ctrl","Alt","Shift"};const int masks[]={pad::Control,pad::Alt,pad::Shift};
      for(int i=0;i<3;++i){float xx=x+10*s+i*120*s;canvas.Rect(xx,y,116*s,23*s,padModifiers&masks[i]?0xff2b4059:0xff202936);canvas.Text(xx+8*s,y+4*s,modifiers[i],TextColor,0,s);buttons.push_back({xx,y,116*s,23*s,386+i});}y+=29*s;
      auto commands=PadCommands();int pages=std::max(1,int(commands.size()+11)/12);padPickerPage=std::clamp(padPickerPage,0,pages-1);
      adjust("Page "+std::to_string(padPickerPage+1)+" / "+std::to_string(pages),302,303);
      for(int i=0;i<12;++i){int index=padPickerPage*12+i;if(index>=int(commands.size()))break;auto label=GamepadCommandName(commands[index]);if(label.size()>42)label=label.substr(0,39)+"...";action(label.c_str(),4000+i);}
      action("Cancel / back to controller bindings",301);
      return;
    }
    if(padListening || padKeyListening){
      canvas.Text(x+14*s,y,"Map controller input to a key or mouse click",Accent,1,s);y+=35*s;
      canvas.Text(x+14*s,y,padListening?"1. Press a button on the controller":"1. Controller: "+std::string(GamepadInputName(padInput)),TextColor,0,s);y+=30*s;
      canvas.Text(x+14*s,y,padKeyListening?"2. Press a key or click left / right / middle":"2. Then press a key or mouse button",Accent,0,s);y+=27*s;
      canvas.Text(x+14*s,y,"Ctrl / Alt / Shift shortcuts are supported.",TextColor,0,s);y+=24*s;
      canvas.Text(x+14*s,y,"Release the captured key or click before playing.",TextColor,0,s);y+=30*s;
      if(padKeyListening)canvas.Text(x+14*s,y,"Press Escape to cancel mapping.",Accent,0,s);
      else action("Cancel mapping (or press Escape)",311);
      return;
    }
    row("Enable gamepad",options.enabled,108);
    canvas.Text(x+14*s,y,GamepadStatus(),TextColor,0,s);y+=24*s;
    action(padDevicePage?"Back to button bindings":"Controller / stick / mouse settings",padDevicePage?331:330);
    adjust(std::string("Selected input: ")+GamepadInputName(padInput),113,114);
    if(padDevicePage){
      const char* backends[]={"Auto","XInput","Generic joystick"};adjust(std::string("Backend: ")+backends[options.backend],144,145);
      adjust("Controller slot: "+std::to_string(options.slot+1),109,110);
      adjust("Stick dead zone: "+std::to_string(int(options.deadzone*100+.5f))+"%",111,112);
      const char* sticks[]={"Off","Left stick","Right stick"};adjust(std::string("Mouse stick: ")+sticks[options.mouseStick],120,121);
      adjust("Mouse speed (px/s): "+std::to_string(int(options.mouseSpeed)),122,123);row("Invert mouse Y",options.invertMouseY,124);
      const char* layouts[]={"Z/R sticks","U/R + Z trigger","Z/R + U/V triggers"};adjust(std::string("Joystick axes: ")+layouts[options.joystickLayout],146,147);
      if(padInput<16)adjust("Joystick button: "+(options.joystickButtons[padInput]<0?std::string("POV / none"):std::to_string(options.joystickButtons[padInput]+1)),148,149);
    }else{
      action("Map: press controller, then keyboard key...",310);
      auto command=GamepadCommandName(options.bindings[padInput]);if(command.size()>38)command=command.substr(0,35)+"...";
      canvas.Text(x+14*s,y,"Assigned: "+command,TextColor,0,s);y+=25*s;
      action("Choose keyboard key / shortcut...",300);
      action("Assign LEFT mouse click",320);action("Assign RIGHT mouse click",321);action("Assign MIDDLE mouse click",322);action("Unbind selected input",323);
      action("Restore default keyboard bindings",118);action("Release held controls",119);
      if(!padBindingFeedback.empty()){canvas.Text(x+14*s,y,padBindingFeedback.substr(0,46),Accent,0,s);y+=22*s;}
      const bool mouseDirection=options.mouseStick && padInput>=16+(options.mouseStick-1)*4 && padInput<20+(options.mouseStick-1)*4;
      canvas.Text(x+14*s,y+8*s,mouseDirection?"Direction is owned by mouse stick settings.":"Match these keys in EverQuest Options / Keys.",TextColor,0,s);
    }
  } else if(menuPage==8){
    auto pipes=GetPipeOptions();row("Named pipes (Zeal data protocol)",pipes.enabled,130);
    row("Verbose group / raid data",pipes.verbose,131);
    row("Zeal endpoint compatibility",pipes.zealAlias,134);
    adjust("Pipe interval (ms): "+std::to_string(pipes.delay),132,133);
    canvas.Text(x+14*s,y+10*s,"Gusto endpoint: gusto_<game process ID>",TextColor,0,s);
    canvas.Text(x+14*s,y+32*s,"Zeal endpoint: zeal_<game process ID>",TextColor,0,s);
    canvas.Text(x+14*s,y+60*s,"/gusto pipes status shows connected readers.",TextColor,0,s);
    canvas.Text(x+14*s,y+82*s,"/pipe <text> sends a custom JSON message.",TextColor,0,s);
  }
}

void DrawEffects(void *render, const D3DVIEWPORT9 &viewport, ULONGLONG now) {
  D3DMATRIX vp{};
  if (!Read(render, 0x16c0, vp))
    return;
  std::erase_if(effects, [&](const auto &e) { return e.Life(now) >= 1; });
  for (const auto &e : effects) {
    auto target=Spawn(e.target);
    float x=0,y=0;
    if (reinterpret_cast<std::uintptr_t>(target)!=e.identity ||
        !ScreenHead(target,vp,viewport,x,y)) continue;
    x+=e.x; y+=e.y-e.Rise(now);
    auto color = e.healing ? 0xff35ef69 : e.target == localId ? 0xffff5757
                 : e.critical        ? 0xffffcc5c
                 : e.spell > 0       ? Accent
                                     : TextColor;
    std::string text = e.healing ? "+"+std::to_string(e.damage)+(e.percent?"%":"") :
      e.damage == 0 ? "MISS" : std::to_string(e.damage);
    canvas.Text(x, y, text, Alpha(color, e.Alpha(now)), e.critical ? 2 : 1,
                cfg.scale, true);
  }
}
void CaptureHit(void *packet) {
  std::uint16_t target = 0, source = 0;
  int spell = -1, damage = -1;
  if (InGame() && Read(packet, 0, target) && Read(packet, 2, source) &&
      Read(packet, 5, spell) && Read(packet, 9, damage) && target &&
      damage >= 0 && damage < 1000000000) {
    Guard guard;
    bool isSpell = spell > 0;
    bool mine = source == localId || (petId && source == petId),
         toMe = target == localId;
    if (cfg.floating && (mine || (toMe && cfg.self) || cfg.others) &&
        (isSpell ? cfg.spells : cfg.melee)) {
      if (effects.size() >= 256)
        effects.erase(effects.begin());
      hud::FloatEffect effect;
      effect.target = target;
      effect.identity = reinterpret_cast<std::uintptr_t>(Spawn(target));
      effect.source = source;
      effect.damage = damage;
      effect.spell = isSpell ? spell : -1;
      effect.born = GetTickCount64();
      effect.critical = damage >= cfg.bigHit;
      effect.y = float(int((effect.born + source * 7) % 41) - 20);
      effect.x = float(
          int((effect.born + target * 17 + effects.size() * 13) % 81) - 40);
      effects.push_back(effect);
    }
  }
}
bool __fastcall HitHookResult(void *self, void *, void *packet, bool flag,
                              int value) {
  CaptureHit(packet);
  return originalHit ? originalHit(self, packet, flag, value) : false;
}
void FlushMouseDevice(IDirectInputDevice8A *device) {
  if (!device) return;
  DIDEVICEOBJECTDATA events[128];
  for (int i=0;i<4;++i) {
    DWORD count=128;
    if (FAILED(device->GetDeviceData(sizeof(DIDEVICEOBJECTDATA),events,&count,0)) || count<128) break;
  }
  DIMOUSESTATE2 state{};
  device->GetDeviceState(sizeof(state),&state);
}
bool OwnNativeMouse() {
  // A world drag that crosses the HUD must still receive its release; that
  // release also restores the cursor hidden by the native mouse-down routine.
  if (worldLeftHeld || worldRightHeld) return false;
  if (!window || !InGame() || GetForegroundWindow()!=window) return false;
  POINT point{}; GetCursorPos(&point); ScreenToClient(window,&point);
  Guard guard;
  return OwnedPoint(float(point.x),float(point.y)) || ownedLeft || ownedRight || dragging ||
    GetTickCount64()<blockLeftUntil || GetTickCount64()<blockRightUntil;
}
void CaptureKeyboardDevice(IDirectInputDevice8A* device){
 if(!device)return;device->Acquire();DIDEVICEOBJECTDATA events[64];
 for(int batch=0;batch<4;++batch){DWORD count=64;auto hr=device->GetDeviceData(sizeof(DIDEVICEOBJECTDATA),events,&count,0);if(FAILED(hr))return;
  for(DWORD i=0;i<count;++i){unsigned scan=events[i].dwOfs&255;bool down=(events[i].dwData&0x80)!=0;
   UINT nativeScan=(scan&0x80)?0xe000u|(scan&0x7f):scan;
   int vk=int(pad::KeyFromDirectInputScan(scan,MapVirtualKeyW(nativeScan,MAPVK_VSC_TO_VK_EX)));if(vk)FeedGamepadKeyboardCapture(vk,down);
   if(!down)ReleaseNativePhysicalKey(scan);
  }
  if(count<64)break;
 }
 BYTE state[256]{};
 if(SUCCEEDED(device->GetDeviceState(sizeof(state),state))){bool held=false;for(BYTE key:state)held=held||bool(key&0x80);DrainGamepadKeyboardCapture(!held);}
}
int __cdecl ProcessKeyboardHook(){
 if(GamepadKeyboardCaptureBlocking() && InGame() && window && GetForegroundWindow()==window){
  auto* device=static_cast<IDirectInputDevice8A*>(Global({0xe67b4c,addresses::Storage::PointerStorage}));CaptureKeyboardDevice(device);return 1;
 }
 return originalProcessKeyboard?originalProcessKeyboard():1;
}
bool FocusedWindowEditsText(void* focusedWindow,bool& editing){
 editing=false;if(!focusedWindow)return true;
 // RoF2 CXWnd::IsType (0x868e10): ArrayClass2 runtime types at +0x44.
 // WRT_EDITWND=2; WRT_EDITHOTKEYWND=8 in the emu-rof2 layout.
 int count=0,shift=0;unsigned mask=0;void* blocks=nullptr;
 if(!Read(focusedWindow,0x44,count)||!Read(focusedWindow,0x4c,mask)||
    !Read(focusedWindow,0x50,shift)||!Read(focusedWindow,0x54,blocks)||
    count<0||count>64||shift<0||shift>16||mask!=((1u<<shift)-1))return false;
 for(int i=0;i<count;++i){void* block=nullptr;unsigned type=0;
  if(!Read(blocks,4*(unsigned(i)>>shift),block)||!Read(block,4*(unsigned(i)&mask),type))return false;
  if(type==2||type==8)editing=true;
 }
 return true;
}
void PollHudGamepad(){
  bool chat=true;int chatMode=0;void* manager=nullptr;void* focusedWindow=nullptr;
  const bool ready=InGame() && Global(LocalPC)!=nullptr;
  // Native GetFocusWnd (0x875eb0) reads +0x64. ManagerMode +0xa4 is
  // set to 20 by HandleKeyboardMsg for ordinary movement key-downs too.
  if(Read(reinterpret_cast<void*>(VA(0xddf66c)),0,chatMode) &&
     Read(reinterpret_cast<void*>(VA(0x15d3d00)),0,manager) && Read(manager,0x64,focusedWindow)){
    bool editing=false;
    if(FocusedWindowEditsText(focusedWindow,editing))chat=pad::KeyboardTypingGate(chatMode,editing);
  }
  const bool foreground=window && GetForegroundWindow()==window;
  const unsigned gateReasons=(!ready?1u:0u)|(!window?2u:0u)|(!foreground?4u:0u)|(menu?8u:0u)|(chat?16u:0u);
  PollGamepad(!gateReasons,ready,window,ready && foreground,gateReasons);
}
void CaptureMappingMouse(IDirectInputDevice8A* device){
 if(!device)return;device->Acquire();DIDEVICEOBJECTDATA events[64]{};
 const int keys[]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON};
 for(int batch=0;batch<4;++batch){DWORD count=64;
  if(FAILED(device->GetDeviceData(sizeof(DIDEVICEOBJECTDATA),events,&count,0)))return;
  for(DWORD i=0;i<count;++i)if(events[i].dwOfs>=DIMOFS_BUTTON0 && events[i].dwOfs<=DIMOFS_BUTTON2)
   FeedGamepadKeyboardCapture(keys[events[i].dwOfs-DIMOFS_BUTTON0],(events[i].dwData&0x80)!=0);
  if(count<64)break;
 }
 DIMOUSESTATE2 state{};
 if(SUCCEEDED(device->GetDeviceState(sizeof(state),&state)))
  for(int i=0;i<3;++i)if(!(state.rgbButtons[i]&0x80))FeedGamepadKeyboardCapture(keys[i],false);
}
void __cdecl ProcessMouseHook() {
  PollHudGamepad();
  const bool mapping=GamepadKeyboardCaptureBlocking() && InGame() && window && GetForegroundWindow()==window;
  if(mapping)CaptureMappingMouse(static_cast<IDirectInputDevice8A*>(Global({0xe67b50,addresses::Storage::PointerStorage})));
  const bool blocked=mapping||OwnNativeMouse();
  if (blocked || flushNativeMouse) {
    auto *device=static_cast<IDirectInputDevice8A*>(Global(
      {0xe67b50,addresses::Storage::PointerStorage}));
    FlushMouseDevice(device);
  }
  flushNativeMouse=blocked;
  if (!blocked && originalProcessMouse) originalProcessMouse();
}
void __fastcall LeftDownHook(void *self,void*,int x,int y) {
  if (!OwnNativeMouse() && originalLeftDown) { worldLeftHeld=true; originalLeftDown(self,x,y); }
}
void __fastcall RightDownHook(void *self,void*,int x,int y) {
  if (!OwnNativeMouse() && originalRightDown) { worldRightHeld=true; originalRightDown(self,x,y); }
}
void __cdecl WheelHook(int delta) {
  bool used = false;
  if (InGame()) {
    Guard guard;
    POINT p{};
    GetCursorPos(&p);
    if (window)
      ScreenToClient(window, &p);
    if (delta && MapHit(float(p.x), float(p.y))) {
      if (GetTickCount64() - wheelMessageTick > 100) {
        cfg.zoom = std::clamp(cfg.zoom + (delta > 0 ? -1 : 1), 0, 4);
        Save();
      }
      used = true;
    } else if (menu && OwnedPoint(float(p.x), float(p.y)))
      used = true;
  }
  if (!used && originalWheel)
    originalWheel(delta);
}
void __fastcall LeftHook(void *self, void *, int x, int y) {
  if (worldLeftHeld.exchange(false)) {
    if (originalLeft) originalLeft(self,x,y);
    return;
  }
  bool used = false;
  if (InGame()) {
    Guard guard;
    used = OwnedPoint(float(x), float(y)) || ownedLeft || GetTickCount64() < blockLeftUntil;
  }
  if (!used && originalLeft)
    originalLeft(self, x, y);
}
void __fastcall RightHook(void *self, void *, int x, int y) {
  if (worldRightHeld.exchange(false)) {
    if (originalRight) originalRight(self,x,y);
    return;
  }
  bool used = false;
  if (InGame()) {
    Guard guard;
    used = OwnedPoint(float(x), float(y)) || ownedRight || GetTickCount64() < blockRightUntil;
  }
  if (!used && originalRight)
    originalRight(self, x, y);
}
void __fastcall CameraHook(void *camera, void *) {
  void *activeCamera = nullptr;
  const bool active = Read(Global(addresses::Display), 0x118, activeCamera) &&
                      activeCamera == camera;
  if (InGame() && active) {
    Guard guard;
    float fov = 0;
    Point pos{}, angles{};
    int type = 0;
    auto now = GetTickCount64();
    if (Read(camera, 4, fov) && Read(camera, 0xbc, pos) &&
        Read(camera, 0xb0, angles) &&
        Read(reinterpret_cast<void *>(VA(0xd1fd9c)), 0, type) &&
        std::isfinite(fov) && std::isfinite(pos.x) && std::isfinite(pos.y) &&
        std::isfinite(pos.z) && std::isfinite(angles.x) &&
        std::isfinite(angles.y) && std::isfinite(angles.z)) {
      if (camera != lastCamera) {
        lastCamera = camera;
        smoothPos = pos;
        smoothAngles = angles;
        cameraTick = now;
        savedFov = fov;
        ownedFov = false;
      }
      if (cfg.camera) {
        if (!ownedFov)
          savedFov = fov;
        if (fov != cfg.fov)
          StoreFloat(static_cast<BYTE *>(camera) + 4, cfg.fov);
        ownedFov = true;
        float dt = float(now - cameraTick) / 1000;
        float dist =
            std::hypot(std::hypot(pos.x - smoothPos.x, pos.y - smoothPos.y),
                       pos.z - smoothPos.z);
        if (type > 0 && type <= 4 && dist < 200 && now - cameraTick < 500) {
          // Keep the native collision-resolved position. Only smooth orientation;
          // interpolating the position after collision resolution can cross walls.
          angles.x =
              hud::SmoothAngle(smoothAngles.x, angles.x, dt, cfg.smoothing);
          angles.y =
              hud::SmoothAngle(smoothAngles.y, angles.y, dt, cfg.smoothing);
          SIZE_T written;
          WriteProcessMemory(GetCurrentProcess(),
                             static_cast<BYTE *>(camera) + 0xb0, &angles,
                             sizeof(angles), &written);
        }
      } else if (ownedFov) {
        StoreFloat(static_cast<BYTE *>(camera) + 4, savedFov);
        ownedFov = false;
      }
      smoothPos = pos;
      smoothAngles = angles;
      cameraTick = now;
    }
  }
  if (originalCamera)
    originalCamera(camera);
}
} // namespace

bool InstallHudHooks(HMODULE module, const ValidatedClient &validation) {
  if (initialized)
    return true;
  if (!validation.IsValidFor(module))
    return false;
  game = module;
  token = validation;
  InitializePipes(game,token);
  Load();
  InitializeGamepad(game,token);
  LoadSkeletonLaugh();
  float zoom = 0;
  if (!Read(reinterpret_cast<void *>(VA(0x9d0de4)), 0, zoom) || zoom != 53) {
    lastError = "Unsupported native zoom constant";
    return false;
  }
  originalMaxZoom = lastMaxZoom = zoom;
  constexpr BYTE keyboardBytes[]={0x81,0xec,0x88,0x02,0,0};
  constexpr BYTE input[]={0x83,0xec,0x14,0xa1,0xcc,0x7c,0xe6,0};
  constexpr BYTE leftDown[]={0xa0,0x02,0xf7,0xdd,0};
  constexpr BYTE rightDown[]={0x83,0xec,0x08,0x81,0x3d,0x8c,0x7b,0xe6,0,0xdd,0xdd,0xdd,0xdd};
  constexpr BYTE hit[] = {0xb8, 0xc4, 0x13, 0, 0};
  constexpr BYTE left[] = {0x6a, 0xff, 0x68, 0xcf, 0x8a, 0x97, 0};
  constexpr BYTE right[] = {0x81, 0xec, 0x54, 3, 0, 0};
  constexpr BYTE wheel[] = {0xff, 0x15, 0x30, 3, 0x9c, 0};
  bool ok = true;
  if(!originalProcessKeyboard){
    bool installed=InstallDetour(game,token,0x5fda30,keyboardBytes,sizeof(keyboardBytes),reinterpret_cast<void*>(&ProcessKeyboardHook),reinterpret_cast<void**>(&originalProcessKeyboard));
    if(!installed){std::array<BYTE,5> entry{};auto address=VA(0x5fda30,5);
      if(address && Read(reinterpret_cast<void*>(address),0,entry) && entry[0]==0xe9)
        installed=InstallDetour(game,token,0x5fda30,entry.data(),entry.size(),reinterpret_cast<void*>(&ProcessKeyboardHook),reinterpret_cast<void**>(&originalProcessKeyboard));
    }
    ok=installed&&ok;
  }
  if (!originalProcessMouse){
    bool installed=InstallDetour(game,token,0x539e60,input,sizeof(input),reinterpret_cast<void*>(&ProcessMouseHook),reinterpret_cast<void**>(&originalProcessMouse));
    if(!installed){std::array<BYTE,5> entry{};auto address=VA(0x539e60,5);if(address&&Read(reinterpret_cast<void*>(address),0,entry)&&entry[0]==0xe9)installed=InstallDetour(game,token,0x539e60,entry.data(),entry.size(),reinterpret_cast<void*>(&ProcessMouseHook),reinterpret_cast<void**>(&originalProcessMouse));}
    ok=installed&&ok;
  }
  if (!originalLeftDown)
    ok=InstallDetour(game,token,0x518870,leftDown,sizeof(leftDown),reinterpret_cast<void*>(&LeftDownHook),reinterpret_cast<void**>(&originalLeftDown)) && ok;
  if (!originalRightDown)
    ok=InstallDetour(game,token,0x518910,rightDown,sizeof(rightDown),reinterpret_cast<void*>(&RightDownHook),reinterpret_cast<void**>(&originalRightDown)) && ok;
  if (!originalHit)
    ok = InstallDetour(game, token, 0x52ee40, hit, sizeof(hit),
                       reinterpret_cast<void *>(&HitHookResult),
                       reinterpret_cast<void **>(&originalHit)) &&
         ok;
  if (!originalLeft)
    ok = InstallDetour(game, token, 0x5384d0, left, sizeof(left),
                       reinterpret_cast<void *>(&LeftHook),
                       reinterpret_cast<void **>(&originalLeft)) &&
         ok;
  if (!originalRight)
    ok = InstallDetour(game, token, 0x537d30, right, sizeof(right),
                       reinterpret_cast<void *>(&RightHook),
                       reinterpret_cast<void **>(&originalRight)) &&
         ok;
  if (!originalWheel)
    ok = InstallDetour(game, token, 0x5f9c90, wheel, sizeof(wheel),
                       reinterpret_cast<void *>(&WheelHook),
                       reinterpret_cast<void **>(&originalWheel)) &&
         ok;
  initialized = true;
  UpdateZoom();
  if (!ok)
    lastError = "One or more optional combat/input hooks were unavailable";
  return ok;
}
void ReadHudLocalResources(int& mana,int& maxMana,int& endurance,int& maxEndurance){
  LocalMana(mana,maxMana);LocalEndurance(endurance,maxEndurance);
}
void TickHudHooks() {
  TickPipes();
  if (!InGame() || GetInitializationStatus() != InitializationStatus::Ready ||
      originalCamera)
    return;
  auto graphics = GetModuleHandleA("EQGraphicsDX9.dll");
  if (!graphics)
    return;
  constexpr BYTE expected[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0};
  if (!InstallModuleDetour(game, token, graphics, 0x6470, expected,
                           sizeof(expected),
                           reinterpret_cast<void *>(&CameraHook),
                           reinterpret_cast<void **>(&originalCamera)))
    lastError = "Camera matrix hook unavailable";
}
void DrawHudWorldBars(IDirect3DDevice9* device,void* render){
  if(!InGame()||!device)return;
  Guard guard;
  auto state=GetClientState();PlayerSnapshot player;
  if(!state||!state->GetPlayerSnapshot(player)||!player.positionValid)return;
  Read(state->GetLocalPlayer(),0x148,localId);
  RefreshHudSpawns(GetTickCount64());
  IDirect3DStateBlock9* saved=nullptr;
  if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&saved)))return;
  if(FAILED(saved->Capture())){saved->Release();return;}
  D3DVIEWPORT9 view{};
  if(SUCCEEDED(device->GetViewport(&view))&&canvas.Init(device)){
    canvas.Begin();DrawOverheadBars(render,view,player,1);canvas.Flush();
  }
  saved->Apply();saved->Release();
}
void DrawHudOverlay(IDirect3DDevice9 *device, void *render) {
  if (!InGame() || !device)
    return;
  PollHudGamepad();
  TickNameplateOptions();
  Guard guard;
  ++hudFrames;
  const auto *state = GetClientState();
  PlayerSnapshot player;
  ZoneSnapshot zone;
  if (!state || !state->GetPlayerSnapshot(player) || !player.positionValid ||
      !player.headingValid || !state->GetZoneSnapshot(zone) ||
      !zone.shortNameValid)
    return;
  Read(state->GetLocalPlayer(), 0x148, localId);
  Read(state->GetLocalPlayer(), 0x2b4, petId);
  if (mapZone!=zone.shortName) {
    previousHealth.clear(); hudSpawns.clear(); spawnTick=0;
  }
  lifeViewer={player.x,player.y,player.z};lifeViewerValid=true;
  RefreshHudSpawns(GetTickCount64());
  // A noncapturing menu/preview press may be released outside the window.
  if (ownedLeft && !dragging && !(GetAsyncKeyState(VK_LBUTTON)&0x8000)) {
    if (arrowSliding) {arrowSliding=false;Save();}
    mapGesture.Release(GetTickCount64()); ownedLeft=false;
  }
  if (ownedRight && !(GetAsyncKeyState(VK_RBUTTON)&0x8000)) ownedRight=false;
  zoneName = zone.longNameValid ? zone.longName : zone.shortName;
  if (mapZone != zone.shortName)
    LoadMap(zone.shortName);
  PublishExternalMap(player);
  D3DDEVICE_CREATION_PARAMETERS creation{};
  if (SUCCEEDED(device->GetCreationParameters(&creation)) &&
      creation.hFocusWindow && window != creation.hFocusWindow) {
    window = creation.hFocusWindow;
    SetLastError(0);
    auto previous = SetWindowLongPtrW(window, GWLP_WNDPROC,
                                      reinterpret_cast<LONG_PTR>(&HudWndProc));
    if (previous)
      previousWndProc = reinterpret_cast<WNDPROC>(previous);
    else
      window = nullptr;
  }
  if (window) {
    GetCursorPos(&mouse);
    ScreenToClient(window, &mouse);
  }
  IDirect3DStateBlock9 *saved = nullptr;
  if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)))
    return;
  if (FAILED(saved->Capture())) {
    saved->Release();
    return;
  }
  D3DVIEWPORT9 viewport{};
  if (FAILED(device->GetViewport(&viewport)) || !canvas.Init(device)) {
    lastError = "HUD font atlas or viewport unavailable";
    saved->Release();
    return;
  }
  canvas.Begin();
  buttons.clear();
  DrawOverheadBars(render,viewport,player,2);
  if (fullMap || mapGesture.preview)
    DrawFullMap(player, viewport);
  if (cfg.floating)
    DrawEffects(render, viewport, GetTickCount64());
  else
    effects.clear();
  if (cfg.minimap)
    DrawMinimap(player, viewport);
  if (menu)
    DrawMenu(viewport);
  canvas.Flush();
  saved->Apply();
  saved->Release();
}
void RemoveHudSpawn(std::uint32_t id,void* object) {
  if (!id) return;
  Guard guard;
  if(object){std::array<BYTE,0x498> bytes{};if(Read(object,0,bytes))CaptureLifeSample(object,bytes,GetTickCount64());}
  ForgetLifeSpawn(id);
  previousHealth.erase(id);
  std::erase_if(hudSpawns,[&](const auto& e){ return e.id==id; });
  std::erase_if(effects,[&](const auto& e){ return e.target==id; });
}
void ResetHudState() {
  CancelGamepadInputCapture();CancelGamepadKeyboardCapture();padListening=padKeyListening=padPicker=false;
  ShutdownPipes();
  ReleaseGamepad(InGame());
  Guard guard;
  ResetLifeEffects();lifeViewerValid=false;
  effects.clear();
  hudSpawns.clear(); previousHealth.clear(); spawnTick=0; mapGesture.Cancel();
  externalMapData.reset(); UpdateExternalMap({}); arrowSliding=false;
  map = {};
  mapZone.clear();
  zoneName.clear();
  localId = petId = 0;
  localManaCurrent=-1; localManaMaximum=0;
  if (ownedFov && lastCamera)
    StoreFloat(static_cast<BYTE *>(lastCamera) + 4, savedFov);
  ownedFov = false;
  lastCamera = nullptr;
  cameraTick = 0;
  menu = fullMap = dragging = false;
  ownedLeft = ownedRight = false;
  flushNativeMouse=false; worldLeftHeld=worldRightHeld=false;
  buttons.clear();
}
void ShutdownHudOverlay() {
  ShutdownPipes();
  ReleaseGamepad(InGame());
  CloseExternalMap();
  Guard guard;
  ResetLifeEffects();
  if (window && previousWndProc &&
      reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC)) ==
          &HudWndProc)
    SetWindowLongPtrW(window, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(previousWndProc));
  window = nullptr;
  previousWndProc = nullptr;
  cfg.camera = false;
  UpdateZoom();
  if (ownedFov && lastCamera)
    StoreFloat(static_cast<BYTE *>(lastCamera) + 4, savedFov);
  ownedFov = false;
  canvas.Release();
}
bool HandleHudCommand(std::string_view text, std::string &reply) {
  if(HandlePipeCommand(text,reply))return true;
  std::istringstream input{std::string(text)};
  std::string cmd, sub, value;
  input >> cmd >> sub >> value;
  bool root = cmd == "/gusto" || cmd == "/zeal";
  if (root && (sub.empty() || sub == "menu" || sub == "options")) {
    if (!initialized) {
      reply = "Gusto HUD hooks unavailable for this client.";
      return true;
    }
    Guard guard;
    menu = !menu;
    return true;
  }
  if(root && sub=="gamepad") {
    if(!initialized){reply="Gusto HUD hooks unavailable for this client.";return true;}
    Guard guard;auto options=GetGamepadOptions();
    if(value.empty()){menu=true;menuPage=7;return true;}
    if(value=="status"){reply=GamepadStatus()+"; "+GamepadKeyboardStatus();return true;}
    if(value=="on" || value=="off")options.enabled=value=="on";
    else if(value=="bind") {
      int index=0;std::string name,extra;input>>index>>name;
      int binding=ParseGamepadBinding(name);
      if(index<1 || index>pad::Count || binding==-2 || (input>>extra)){
        reply="Use /gusto gamepad bind <input 1-24> <key, Ctrl+key, MOUSE_LEFT or UNBOUND>.";return true;
      }
      options.bindings[index-1]=binding;
      reply=std::string(GamepadInputName(index-1))+" -> "+GamepadCommandName(binding);
    } else {reply="Use /gusto gamepad [on|off|bind <input 1-24> <KEY>].";return true;}
    ReleaseGamepad(InGame());SetGamepadOptions(options);Save();return true;
  }
  bool mapCmd = cmd == "/map" || cmd == "/minimap" ||
                (root && (sub == "map" || sub == "minimap"));
  bool floatCmd = cmd == "/fcd" || (root && sub == "floating");
  bool camCmd = cmd == "/camera" || (root && sub == "camera");
  bool status = root && sub == "status";
  if (!mapCmd && !floatCmd && !camCmd && !status)
    return false;
  if (root && !status) {
    sub = value;
    input >> value;
  }
  if (!initialized) {
    reply = "Gusto HUD hooks unavailable for this client.";
    return true;
  }
  Guard guard;
  auto toggle = [](bool &b, const std::string &s) {
    if (s == "on")
      b = true;
    else if (s == "off")
      b = false;
    else
      b = !b;
  };
  auto parse = [](const std::string &s, float &f) {
    char *end = nullptr;
    f = std::strtof(s.c_str(), &end);
    return !s.empty() && end == s.c_str() + s.size() && std::isfinite(f);
  };
  float f = 0;
  if (status) {
    reply = "Gusto HUD: frames=" + std::to_string(hudFrames.load()) +
            ", map lines=" + std::to_string(map.lines.size()) +
            ", effects=" + std::to_string(effects.size()) +
            ", gamepad=" + GamepadStatus() + ", death/FD=" + std::to_string(LifeEffectCount()) + ", laugh=" + std::to_string(skeletonLaughId) +
            ", spawns=" + std::to_string(hudSpawns.size()) +
            ", local mana=" + std::to_string(localManaCurrent) + "/" + std::to_string(localManaMaximum) +
            ", input=" + (originalProcessMouse && originalLeftDown && originalRightDown ? "attached" : "unavailable") +
            ", camera=" + (originalCamera ? "attached" : "waiting") +
            ", settings=" + (settingsPath.empty() ? "unavailable" : "saved");
    if (!lastError.empty())
      reply += "; " + lastError;
    return true;
  }
  if (mapCmd) {
    if (sub.empty() || sub == "on" || sub == "off")
      toggle(cfg.minimap, sub);
    else if (sub == "rotate")
      toggle(cfg.rotate, value);
    else if (sub == "autoz")
      toggle(cfg.autoZ, value);
    else if (sub == "reload")
      mapZone.clear();
    else if (sub == "full")
      fullMap = !fullMap;
    else if (sub == "external")
      OpenHudExternalMap();
    else if (sub == "scale" && parse(value, f) && f >= .5f && f <= 3)
      cfg.scale = f;
    else if (sub == "zoom" && parse(value, f) && f >= 1 && f <= 5)
      cfg.zoom = int(f) - 1;
    else if (sub == "group")
      toggle(cfg.group, value);
    else if (sub == "labels")
      toggle(cfg.labels, value);
    else {
      reply =
          "Usage: /minimap [on|off|rotate|autoz|group|labels|reload|full|zoom "
          "1..5|scale 0.5..3]";
      return true;
    }
    reply = std::string("Minimap ") + (cfg.minimap ? "on" : "off") +
            "; range " + std::to_string(int(hud::MapRanges[cfg.zoom]));
  } else if (floatCmd) {
    if (sub.empty() || sub == "on" || sub == "off")
      toggle(cfg.floating, sub);
    else if (sub == "bighit" && parse(value, f) && f >= 1 && f <= 100000000)
      cfg.bigHit = int(f);
    else if (sub == "healing")
      toggle(cfg.healing,value);
    else if (sub == "melee")
      toggle(cfg.melee, value);
    else if (sub == "spells")
      toggle(cfg.spells, value);
    else if (sub == "self")
      toggle(cfg.self, value);
    else if (sub == "others")
      toggle(cfg.others, value);
    else {
      reply = "Usage: /fcd [on|off|healing on/off|melee on/off|spells on/off|self "
              "on/off|others on/off|bighit N]";
      return true;
    }
    reply =
        std::string("Floating combat text ") + (cfg.floating ? "on" : "off");
  } else if (camCmd) {
    if (sub.empty() || sub == "on" || sub == "off")
      toggle(cfg.camera, sub);
    else if (sub == "fov" && parse(value, f) && f >= 10 && f <= 100)
      cfg.fov = f;
    else if (sub == "zoom" && parse(value, f) && f >= 5 && f <= 200)
      cfg.maxZoom = f;
    else if (sub == "smooth" && parse(value, f) && f >= 0 && f <= .5f)
      cfg.smoothing = f;
    else if (sub == "mode" && parse(value, f) && f >= 0 && f <= 4 &&
             std::floor(f) == f) {
      auto p = VA(0xd1fd9c);
      int v = int(f);
      SIZE_T written;
      if (p)
        WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(p), &v,
                           4, &written);
    } else {
      reply = "Usage: /camera [on|off|fov 10..100|zoom 5..200|smooth "
              "0..0.5|mode 0..4]";
      return true;
    }
    UpdateZoom();
    reply = std::string("Camera enhancements ") + (cfg.camera ? "on" : "off") +
            "; FOV " + std::to_string(int(cfg.fov)) + "; zoom " +
            std::to_string(int(cfg.maxZoom));
  }
  Save();
  return true;
}
} // namespace gusto::client::rof2
