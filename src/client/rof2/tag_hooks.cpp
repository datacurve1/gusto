#include "gusto/client/rof2/tag_hooks.hpp"
#include "gusto/client/rof2/hud_overlay.hpp"
#include "gusto/client/rof2/named_pipe.hpp"
#include "gusto/client/rof2/life_fx.hpp"
#include "gusto/client/rof2/addresses.hpp"
#include "gusto/client/rof2/globals.hpp"
#include "gusto/client/rof2/hook_utils.hpp"
#include "gusto/client/rof2/initialization.hpp"
#include "gusto/client/rof2/nameplate_hooks.hpp"
#include "gusto/client/rof2/tag_channels.hpp"
#include "gusto/client/rof2/tag_models.hpp"
#include "gusto/client/rof2/tag_state.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include <limits>
#include <vector>

namespace gusto::client::rof2 {
namespace {
// May 10 2013 RoF2: eqlib emu-rof2 offsets; graphics corrections verified
// against EQGraphicsDX9.dll. See docs/tagging.md for provenance and byte
// checks.
constexpr addresses::Global SpawnManager{0x00e641d0,
                                         addresses::Storage::PointerStorage};
constexpr addresses::Global GraphicsEngine{0x015d4698,
                                           addresses::Storage::PointerStorage};
constexpr addresses::Global EverQuest{0x00e67ccc,
                                      addresses::Storage::PointerStorage};
constexpr addresses::Global LocalPC{0x00dd261c,
                                    addresses::Storage::PointerStorage};
constexpr addresses::Global Raid{0x00dd2690, addresses::Storage::DirectObject};
constexpr std::size_t ActorOffset = 0x101c, IdOffset = 0x148, NameOffset = 0xa4,
                      TypeOffset = 0x125;
constexpr std::size_t DeviceOffset = 0xf08, CallbackOffset = 0xa9c4;
using GetSpawnFn = void *(__thiscall *)(void *, int);
using NameStateFn = int(__thiscall *)(void *, bool);
using ChatFn = void(__thiscall *)(void *, const char *, int, bool, bool);
using GameStateFn = void(__thiscall *)(void *, int);
using RemoveSpawnFn = void *(__thiscall *)(void *, void *);
using RenderCallback = void (*)();
using PrimitiveRenderFn = void(__thiscall *)(void *, bool, bool);
PrimitiveRenderFn originalPrimitives = nullptr;
std::atomic_bool drawPending{false};
std::atomic_bool overlayPending{false};
std::atomic_ulong latePasses{0};
std::atomic<float> lastClipX{0}, lastClipY{0}, lastClipZ{0}, lastClipW{0};
HMODULE module = nullptr;
ValidatedClient validation;
GetSpawnFn getSpawn = nullptr;
NameStateFn refreshName = nullptr;
ChatFn originalChat = nullptr;
GameStateFn originalGameState = nullptr;
RemoveSpawnFn originalRemoveSpawn = nullptr;
SRWLOCK lock = SRWLOCK_INIT;
TagState tags;
std::atomic_bool suppress{false}, filter{false}, prettyprint{false};
std::atomic_bool showModels{true}, showText{true}, optionsDirty{false}, clearRequested{false};
std::string tagChannel;
void *hookedRender = nullptr;
RenderCallback previousRender = nullptr;
std::atomic_bool ready{false};
std::atomic_ulong renderFrames{0}, renderDraws{0};
std::atomic<HRESULT> renderResult{S_OK};
std::atomic<const char *> renderStage{"not attached"};
void RenderFailure(const char *stage, HRESULT result) {
  renderResult = result;
  renderStage = stage;
}

bool Readable(const void *source, std::size_t size) {
  auto p = reinterpret_cast<std::uintptr_t>(source);
  if (!p || size > std::numeric_limits<std::uintptr_t>::max() - p)
    return false;
  const auto end = p + size;
  while (p < end) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void *>(p), &info, sizeof(info)) ||
        info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
      return false;
    const auto protection = info.Protect & 255;
    if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
        protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE &&
        protection != PAGE_EXECUTE_WRITECOPY)
      return false;
    const std::uintptr_t next =
        reinterpret_cast<std::uintptr_t>(info.BaseAddress) +
        static_cast<std::uintptr_t>(info.RegionSize);
    if (next <= p)
      return false;
    p = (std::min)(end, next);
  }
  return true;
}
template <class T> bool Read(const void *object, std::size_t offset, T &value) {
  auto base = reinterpret_cast<std::uintptr_t>(object);
  if (!base || offset > std::numeric_limits<std::uintptr_t>::max() - base)
    return false;
  auto source = reinterpret_cast<const void *>(base + offset);
  if (!Readable(source, sizeof(T)))
    return false;
  std::memcpy(&value, source, sizeof(T));
  return true;
}
bool Text(const char *source, std::string &out, std::size_t max = 512) {
  out.clear();
  for (std::size_t i = 0; i < max; ++i) {
    char c;
    if (!Read(source, i, c))
      return false;
    if (!c)
      return true;
    out += c;
  }
  out.clear();
  return false;
}
void *Global(addresses::Global global, std::size_t size = sizeof(void *)) {
  return global.storage == addresses::Storage::DirectObject
             ? ResolveObject(module, validation, global, size)
             : ResolvePointer(module, validation, global, size);
}
bool InGame() {
  int state = 0;
  return ready && Read(Global(EverQuest), 0x5c8, state) && state == 5;
}
struct Spawn {
  void *object = nullptr;
  void *actor = nullptr;
  std::uint32_t id = 0;
  BYTE type = 255;
  std::string name;
  float x = 0, y = 0, z = 0, height = 0;
};
bool Snapshot(void *object, Spawn &out) {
  out = {};
  out.object = object;
  char rawName[64]{};
  if (!Readable(object, addresses::PlayerClientReferenceSize) ||
      !Read(object, IdOffset, out.id) || !out.id ||
      !Read(object, TypeOffset, out.type) || out.type > 1 ||
      !Read(object, ActorOffset, out.actor) || !out.actor ||
      !Read(object, NameOffset, rawName) ||
      !std::memchr(rawName, 0, sizeof(rawName)))
    return false;
  out.name = rawName;
  return !out.name.empty() && Read(object, 0x64, out.y) &&
         Read(object, 0x68, out.x) && Read(object, 0x6c, out.z) &&
         Read(object, 0x13c, out.height) && std::isfinite(out.x) &&
         std::isfinite(out.y) && std::isfinite(out.z) &&
         std::isfinite(out.height) && out.height >= 0 && out.height < 1000;
}
bool Lookup(std::uint32_t id, Spawn &out) {
  void *manager = Global(SpawnManager, 0x18);
  return manager && getSpawn && id <= 0x7fffffffu &&
         Snapshot(getSpawn(manager, int(id)), out) && out.id == id;
}
std::vector<std::pair<std::uint32_t, TagEntry>> Entries() {
  AcquireSRWLockShared(&lock);
  std::vector<std::pair<std::uint32_t, TagEntry>> result;
  if (tags.enabled)
    for (const auto &pair : tags.Entries())
      result.push_back(pair);
  ReleaseSRWLockShared(&lock);
  return result;
}
void Refresh(const std::vector<std::pair<std::uint32_t, TagEntry>> &entries) {
  if (!InGame() || !refreshName)
    return;
  for (const auto &[id, entry] : entries) {
    Spawn spawn;
    if (Lookup(id, spawn) && NormalizeTagName(spawn.name) == entry.name)
      refreshName(spawn.object, true);
  }
}
void ResetTags() {
  auto old = Entries();
  AcquireSRWLockExclusive(&lock);
  tags.Clear();
  ReleaseSRWLockExclusive(&lock);
  Refresh(old);
}
bool Apply(const TagMessage &message) {
  if (!InGame())
    return false;
  Spawn spawn;
  if (message.text != "clear" &&
      (!Lookup(message.spawnId, spawn) ||
       NormalizeTagName(spawn.name) != NormalizeTagName(message.name)))
    return false;
  auto old = Entries();
  AcquireSRWLockExclusive(&lock);
  const bool result = tags.Apply(message, spawn.type == 1);
  ReleaseSRWLockExclusive(&lock);
  if (result) {
    if (message.spawnId)
      old.emplace_back(message.spawnId,
                       TagEntry{NormalizeTagName(message.name), {}, 0});
    Refresh(old);
  }
  return result;
}
bool InRaid() {
  int state = 0;
  return Read(Global(Raid, 0x3490), 0x3488, state) && state == 4;
}
bool InGroup() {
  void *group = nullptr;
  if (!Read(Global(LocalPC), 0x31cc, group) || !group)
    return false;
  int count = 0;
  for (int i = 0; i < 6; ++i) {
    void *member = nullptr;
    if (Read(group, 4 + i * 4, member) && member)
      ++count;
  }
  return count > 1;
}
std::string ChannelName() {
  AcquireSRWLockShared(&lock);
  const std::string name = tagChannel;
  ReleaseSRWLockShared(&lock);
  return name;
}
void SetChannelName(std::string_view name) {
  AcquireSRWLockExclusive(&lock);
  tagChannel = name;
  ReleaseSRWLockExclusive(&lock);
}
void NativeCommand(const std::string &text) {
  using InterpretFn = void(__thiscall *)(void *, void *, const char *);
  const auto address = ResolvePreferredVA(module, 0x51fce0, 1);
  const auto *state = GetClientState();
  auto *game = Global(EverQuest);
  if (address && state && state->GetLocalPlayer() && game)
    reinterpret_cast<InterpretFn>(address)(game, state->GetLocalPlayer(),
                                           text.c_str());
}
std::vector<std::string> JoinedChannels(std::size_t proxyOffset) {
  void *proxy = nullptr, *list = nullptr;
  int count = 0;
  if (!Read(Global(EverQuest), proxyOffset, proxy) || !proxy ||
      !Read(proxy, 0x10, count) || count < 0 || count > 10)
    return {};
  if (!count)
    return {};
  if (!Read(proxy, 0xc, list) || !list)
    return {};
  std::vector<std::string> channels;
  for (int i = 0; i < count; ++i) {
    const char *name = nullptr;
    std::string text;
    // Preserve each slot number, including an unavailable slot. Never shift
    // later channels into an earlier native /1, /2, ... destination.
    if (!Read(list, i * 4, name) || !Text(name, text, 48))
      text.clear();
    channels.push_back(std::move(text));
  }
  return channels;
}
int ChannelNumber() {
  const auto current = JoinedChannels(0x0c);
  const auto service = JoinedChannels(0x2a4);
  return FindTagChannelNumber(ChannelName(), current, service);
}

void __fastcall ChatHook(void *self, void *, const char *message, int color,
                         bool log, bool percent) {
  std::string text;
  std::string pipeText;if(Text(message,pipeText))PipeChat(pipeText.c_str(),color);
  const bool group = color == 258 || color == 309 || color == 327;
  const int number = ChannelNumber();
  const bool channel =
      number && (color == 290 + number || color == 314 + number);
  bool handled = false;
  std::string replacement;
  AcquireSRWLockShared(&lock);
  const bool enabled = tags.enabled;
  ReleaseSRWLockShared(&lock);
  if (enabled && (group || channel) && Text(message, text)) {
    const auto quote = text.find('\'');
    auto contents = std::string_view(text);
    if (quote != text.npos && text.size() > quote + 1 && text.back() == '\'')
      contents = contents.substr(quote + 1, text.size() - quote - 2);
    if (auto parsed = ParseTagMessage(contents)) {
      if (parsed->text.starts_with(TagChannelAnnouncement)) {
        const auto name = std::string_view(parsed->text)
                              .substr(TagChannelAnnouncement.size());
        handled = IsTagChannelName(name);
        if (handled && ChannelName() != name) {
          SetChannelName(name);
          NativeCommand("/join " + std::string(name));
        }
      } else
        handled = Apply(*parsed);
      if (handled && prettyprint)
        replacement = parsed->text + " => " + parsed->name;
    }
  }
  if (handled && (suppress || filter))
    return;
  if (originalChat)
    originalChat(self, replacement.empty() ? message : replacement.c_str(),
                 color, log, percent);
}
void *__fastcall RemoveSpawnHook(void *self, void *, void *spawn) {
  std::uint32_t id = 0;
  if (Read(spawn, IdOffset, id) && id) {
    RemoveHudSpawn(id,spawn);
    AcquireSRWLockExclusive(&lock);
    tags.Clear(id);
    ReleaseSRWLockExclusive(&lock);
  }
  return originalRemoveSpawn ? originalRemoveSpawn(self, spawn) : nullptr;
}
void __fastcall GameStateHook(void *self, void *, int state) {
  ResetHudState();
  // Drop identities before any zone or character transition can recycle spawn
  // IDs.
  AcquireSRWLockExclusive(&lock);
  tags.Clear();
  ReleaseSRWLockExclusive(&lock);
  if (originalGameState)
    originalGameState(self, state);
}

// The getter at graphics + 0x54560 returns actor + 0x114. Unlike the
// gameplay Y/X fields, these are the coordinates consumed by the renderer.
bool ActorPosition(void *actor, float (&position)[3]) {
  const auto graphics = GetModuleHandleA("EQGraphicsDX9.dll");
  if (!graphics)
    return false;
  const auto getter = reinterpret_cast<std::uintptr_t>(graphics) + 0x54560;
  constexpr BYTE expected[] = {0x8d, 0x81, 0x14, 0x01, 0, 0, 0xc3};
  void *table = nullptr;
  void *function = nullptr;
  if (!Readable(reinterpret_cast<void *>(getter), sizeof(expected)) ||
      std::memcmp(reinterpret_cast<void *>(getter), expected,
                  sizeof(expected)) ||
      !Read(actor, 0, table) || !Read(table, 0x7c, function) ||
      reinterpret_cast<std::uintptr_t>(function) != getter ||
      !Read(actor, 0x114, position))
    return false;
  return std::all_of(std::begin(position), std::end(position),
                     [](float f) { return std::isfinite(f); });
}

void DrawTagModels() {
  if (!showModels || !InGame())
    return;
  const auto entries = Entries();
  if (entries.empty()) {
    renderStage = "no tags";
    return;
  }
  IDirect3DDevice9 *device = nullptr;
  D3DMATRIX viewProjection{};
  if (!Read(hookedRender, DeviceOffset, device) || !device ||
      !Readable(device, sizeof(void *)) ||
      !Read(hookedRender, 0x16c0, viewProjection)) {
    RenderFailure("device/matrix read", E_POINTER);
    return;
  }
  HRESULT result = device->TestCooperativeLevel();
  if (FAILED(result)) {
    RenderFailure("device lost", result);
    return;
  }
  for (const auto &row : viewProjection.m)
    for (float value : row)
      if (!std::isfinite(value)) {
        RenderFailure("invalid camera", E_INVALIDARG);
        return;
      }
  IDirect3DStateBlock9 *saved = nullptr;
  result = device->CreateStateBlock(D3DSBT_ALL, &saved);
  if (FAILED(result)) {
    RenderFailure("create state", result);
    return;
  }
  result = saved->Capture();
  if (FAILED(result)) {
    RenderFailure("capture state", result);
    saved->Release();
    return;
  }
  D3DMATRIX world{}, view{}, projection{};
  // Pure devices can reject GetTransform. D3DSBT_ALL still restores the
  // transforms we change; use explicit restoration when reads are supported.
  const bool haveTransforms =
      SUCCEEDED(device->GetTransform(D3DTS_WORLD, &world)) &&
      SUCCEEDED(device->GetTransform(D3DTS_VIEW, &view)) &&
      SUCCEEDED(device->GetTransform(D3DTS_PROJECTION, &projection));
  D3DMATRIX identity{};
  for (int i = 0; i < 4; ++i)
    identity.m[i][i] = 1;
  device->SetVertexShader(nullptr);
  device->SetPixelShader(nullptr);
  result = device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
  if (FAILED(result)) {
    RenderFailure("vertex format", result);
    saved->Apply();
    saved->Release();
    return;
  }
  // User-memory draws cannot inherit the engine's instancing stream flags.
  device->SetStreamSourceFreq(0, 1);
  device->SetStreamSourceFreq(1, 1);
  device->SetTexture(0, nullptr);
  device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
  device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
  device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
  device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
  device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
  device->SetRenderState(D3DRS_LIGHTING, FALSE);
  device->SetRenderState(D3DRS_FOGENABLE, FALSE);
  device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
  device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
  device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
  device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
  device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
  device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
  device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
  device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
  device->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
  device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
  device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
  device->SetRenderState(D3DRS_DEPTHBIAS, 0);
  device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);
  result = device->SetTransform(D3DTS_VIEW, &identity);
  if (SUCCEEDED(result))
    result = device->SetTransform(D3DTS_PROJECTION, &viewProjection);
  if (FAILED(result)) {
    RenderFailure("camera transform", result);
    saved->Apply();
    saved->Release();
    return;
  }
  renderStage = "no drawable tags";
  renderResult = S_OK;
  static const TagModels models;
  Spawn player;
  const auto *state = GetClientState();
  const bool havePlayer = state && Snapshot(state->GetLocalPlayer(), player);
  for (const auto &[id, entry] : entries) {
    if (entry.color == TagOff)
      continue;
    Spawn spawn;
    if (!Lookup(id, spawn) || entry.name != NormalizeTagName(spawn.name))
      continue;
    auto shape = entry.color == TagPaw    ? TagModels::Shape::Paw
                 : entry.color == TagStop ? TagModels::Shape::Octagon
                                          : TagModels::Shape::Arrow;
    auto mesh = models.Get(shape, entry.color == TagNameplate
                                      ? GetNameplateTagColor(spawn.object)
                                      : entry.color);
    float position[3]{}, playerPosition[3]{};
    if (!ActorPosition(spawn.actor, position)) {
      RenderFailure("actor graphics position unavailable", E_POINTER);
      continue;
    }
    auto transform = identity;
    if (shape != TagModels::Shape::Arrow && havePlayer &&
        ActorPosition(player.actor, playerPosition)) {
      const float angle = std::atan2(playerPosition[0] - position[0],
                                     playerPosition[1] - position[1]);
      transform.m[0][0] = std::cos(angle);
      transform.m[0][1] = -std::sin(angle);
      transform.m[1][0] = std::sin(angle);
      transform.m[1][1] = std::cos(angle);
    }
    transform.m[3][0] = position[0];
    transform.m[3][1] = position[1];
    transform.m[3][2] = position[2] + spawn.height + 1.5f;
    float clip[4]{};
    for (int c = 0; c < 4; ++c)
      clip[c] = transform.m[3][0] * viewProjection.m[0][c] +
                transform.m[3][1] * viewProjection.m[1][c] +
                (transform.m[3][2] + 2.f) * viewProjection.m[2][c] +
                viewProjection.m[3][c];
    lastClipW = clip[3];
    if (std::isfinite(clip[3]) && std::fabs(clip[3]) > 0.0001f) {
      lastClipX = clip[0] / clip[3];
      lastClipY = clip[1] / clip[3];
      lastClipZ = clip[2] / clip[3];
    }
    result = device->SetTransform(D3DTS_WORLD, &transform);
    if (SUCCEEDED(result))
      result = device->DrawIndexedPrimitiveUP(
          D3DPT_TRIANGLESTRIP, 0, mesh.vertices.size(), mesh.indices.size() - 2,
          mesh.indices.data(), D3DFMT_INDEX16, mesh.vertices.data(),
          sizeof(TagMesh::Vertex));
    renderResult = result;
    if (SUCCEEDED(result)) {
      ++renderDraws;
      renderStage = "drawing";
    } else
      renderStage = "draw failed";
  }
  saved->Apply();
  if (haveTransforms) {
    device->SetTransform(D3DTS_WORLD, &world);
    device->SetTransform(D3DTS_VIEW, &view);
    device->SetTransform(D3DTS_PROJECTION, &projection);
  }
  saved->Release();
}
// Keep the early callback for frame identity; draw only once the world is
// complete, before C2DPrimitiveManager emits its first label/UI pass.
void RenderTags() {
  ++renderFrames;
  if (previousRender)
    previousRender();
  drawPending = InGame();
  overlayPending = InGame();
  renderStage = "queued for late pass";
}
void __fastcall PrimitiveRenderHook(void *self, void *, bool firstPass,
                                    bool deferredPass) {
  ++latePasses;
  if (firstPass && !deferredPass && drawPending.exchange(false)) {
    IDirect3DDevice9* device=nullptr;D3DMATRIX matrix{};hud::Point camera{};void* activeCamera=nullptr;
    if(Read(hookedRender,DeviceOffset,device) && Read(hookedRender,0x16c0,matrix)) {
      Read(Global(addresses::Display),0x118,activeCamera);Read(activeCamera,0xbc,camera);
      DrawLifeEffects(device,matrix,camera,GetTickCount64());
    }
    DrawTagModels();
    // World depth is still bound here. Later 2D/UI passes may clear or
    // replace it, so depth-tested NPC bars must precede those passes.
    if(device)DrawHudWorldBars(device,hookedRender);
  }
  if (originalPrimitives)
    originalPrimitives(self, firstPass, deferredPass);
  if (!firstPass && !deferredPass && overlayPending.exchange(false)) {
    IDirect3DDevice9* device = nullptr;
    if (Read(hookedRender, DeviceOffset, device)) DrawHudOverlay(device, hookedRender);
  }
}
} // namespace

void TickTags() {
  // Bootstrap only after the initialization worker has finished installing
  // game hooks; two patching threads must never suspend one another.
  if (!InGame() || GetInitializationStatus() != InitializationStatus::Ready)
    return;
  if (optionsDirty.exchange(false) || clearRequested.load()) {
    std::vector<std::pair<std::uint32_t, TagEntry>> old;
    AcquireSRWLockExclusive(&lock);
    for (const auto& pair : tags.Entries()) old.push_back(pair);
    if (clearRequested.exchange(false)) tags.Clear();
    ReleaseSRWLockExclusive(&lock);
    Refresh(old);
  }
  TickHudHooks();
  void *engine = Global(GraphicsEngine, 16);
  void *render = nullptr;
  if (!Read(engine, 12, render) || !render) {
    renderStage = "no renderer";
    return;
  }
  void *current = nullptr;
  if (originalPrimitives && render == hookedRender &&
      Read(render, CallbackOffset, current) &&
      current == reinterpret_cast<void *>(&RenderTags))
    return;
  const auto graphics = GetModuleHandleA("EQGraphicsDX9.dll");
  if (!graphics) {
    renderStage = "no graphics DLL";
    return;
  }
  const auto base = reinterpret_cast<std::uintptr_t>(graphics);
  // These instructions have no relocated operands. Reject an unknown graphics
  // build.
  constexpr BYTE setter[] = {0x8b, 0x54, 0x24, 0x04, 0x8b, 0x81, 0xc4,
                             0xa9, 0,    0,    0x89, 0x91, 0xc4, 0xa9,
                             0,    0,    0xc2, 4,    0};
  constexpr BYTE caller[] = {0x8b, 0x86, 0xc4, 0xa9, 0,    0,
                             0x3b, 0xc3, 0x74, 2,    0xff, 0xd0};
  const auto *setterAddress = reinterpret_cast<const void *>(base + 0x92540);
  const auto *callerAddress = reinterpret_cast<const void *>(base + 0x97729);
  if (!Readable(setterAddress, sizeof(setter)) ||
      std::memcmp(setterAddress, setter, sizeof(setter)) ||
      !Readable(callerAddress, sizeof(caller)) ||
      std::memcmp(callerAddress, caller, sizeof(caller))) {
    renderStage = "graphics signature mismatch";
    return;
  }
  if (!originalPrimitives) {
    // sub esp,0x20; cmp byte ptr [esp+0x24],0: two complete,
    // position-independent instructions, confirmed in the supported DLL.
    constexpr BYTE primitiveBytes[] = {0x83, 0xec, 0x20, 0x80,
                                       0x7c, 0x24, 0x24, 0};
    if (!InstallModuleDetour(module, validation, graphics, 0xae370,
                             primitiveBytes, sizeof(primitiveBytes),
                             reinterpret_cast<void *>(&PrimitiveRenderHook),
                             reinterpret_cast<void **>(&originalPrimitives))) {
      renderStage = "late-pass hook unavailable";
      return;
    }
  }
  void *old = nullptr;
  if (!Read(render, CallbackOffset, old)) {
    renderStage = "callback unreadable";
    return;
  }
  auto **slot =
      reinterpret_cast<void **>(static_cast<BYTE *>(render) + CallbackOffset);
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(slot, &info, sizeof(info))) {
    renderStage = "callback page unavailable";
    return;
  }
  const DWORD protection = info.Protect & 255;
  if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
      protection != PAGE_EXECUTE_READWRITE &&
      protection != PAGE_EXECUTE_WRITECOPY) {
    renderStage = "callback not writable";
    return;
  }
  if (old == reinterpret_cast<void *>(&RenderTags))
    return;
  previousRender = reinterpret_cast<RenderCallback>(old);
  hookedRender = render;
  renderStage = "attached, awaiting frame";
  if (InterlockedCompareExchangePointer(
          slot, reinterpret_cast<void *>(&RenderTags), old) != old) {
    hookedRender = nullptr;
    previousRender = nullptr;
    renderStage = "callback changed";
  }
}
std::string GetActorTagText(void *actor) {
  if (!showText || !actor || !InGame())
    return {};
  for (const auto &[id, entry] : Entries()) {
    if (entry.text.empty())
      continue;
    Spawn spawn;
    if (Lookup(id, spawn) && spawn.actor == actor && spawn.type == 1 &&
        entry.name == NormalizeTagName(spawn.name))
      return entry.text;
  }
  return {};
}

TagDisplayOptions GetTagDisplayOptions() {
  AcquireSRWLockShared(&lock);
  TagDisplayOptions o{tags.enabled, showModels.load(), showText.load(),
    tags.defaultArrow, filter.load(), suppress.load(), prettyprint.load()};
  ReleaseSRWLockShared(&lock);
  return o;
}
void SetTagDisplayOptions(const TagDisplayOptions& o) {
  AcquireSRWLockExclusive(&lock);
  tags.enabled=o.enabled; tags.defaultArrow=o.defaultArrow;
  showModels=o.models; showText=o.text;
  filter=o.filter; suppress=o.suppress; prettyprint=o.prettyprint;
  ReleaseSRWLockExclusive(&lock);
  optionsDirty=true;
}
void RequestClearTags() { clearRequested=true; }

bool HandleTagCommand(std::string_view command, void *context,
                      TagSendCommand send, std::string &reply) {
  if (!ready) {
    reply = "Tagging hooks unavailable for this client.";
    return true;
  }
  TickTags();
  while (!command.empty() &&
         (command.front() == ' ' || command.front() == '\t'))
    command.remove_prefix(1);
  auto space = command.find_first_of(" \t");
  auto verb = command.substr(0, space);
  auto text =
      space == command.npos ? std::string_view{} : command.substr(space + 1);
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    text.remove_prefix(1);
  if (verb == "status") {
    AcquireSRWLockShared(&lock);
    reply = std::string("Tags: ") + (tags.enabled ? "on" : "off") +
            "; tracked: " + std::to_string(tags.Entries().size()) +
            "; models: " + (hookedRender ? "attached" : "waiting/unavailable");
    ReleaseSRWLockShared(&lock);
    char details[256]{};
    std::snprintf(details, sizeof(details),
                  "; frames: %lu; draws: %lu; stage: %s; hr: %08lX",
                  renderFrames.load(), renderDraws.load(), renderStage.load(),
                  static_cast<unsigned long>(renderResult.load()));
    reply += details;
    std::snprintf(
        details, sizeof(details), "; late: %lu; clip: %.2f/%.2f/%.2f; w: %.2f",
        latePasses.load(), double(lastClipX.load()), double(lastClipY.load()),
        double(lastClipZ.load()), double(lastClipW.load()));
    reply += details;
    const auto selected = ChannelName();
    const int number = ChannelNumber();
    reply +=
        "; channel: " + (selected.empty() ? std::string("none") : selected) +
        (number ? " (/" + std::to_string(number) + ")" : " (not joined)") +
        "; current/service channels: " +
        std::to_string(JoinedChannels(0x0c).size()) + "/" +
        std::to_string(JoinedChannels(0x2a4).size());
    return true;
  }
  if (verb == "on" || verb == "off") {
    auto old = Entries();
    AcquireSRWLockExclusive(&lock);
    tags.enabled = verb == "on";
    if (!tags.enabled)
      tags.Clear();
    ReleaseSRWLockExclusive(&lock);
    Refresh(old);
    reply =
        verb == "on" ? "Nameplate tags enabled." : "Nameplate tags disabled.";
    return true;
  }
  if (verb == "filter" || verb == "suppress" || verb == "prettyprint" ||
      verb == "defaultarrow") {
    if (text != "on" && text != "off") {
      reply = "Usage: /tag <filter|suppress|prettyprint|defaultarrow> <on|off>";
      return true;
    }
    if (verb == "filter")
      filter = text == "on";
    if (verb == "suppress")
      suppress = text == "on";
    if (verb == "prettyprint")
      prettyprint = text == "on";
    if (verb == "defaultarrow") {
      AcquireSRWLockExclusive(&lock);
      tags.defaultArrow = text == "on";
      ReleaseSRWLockExclusive(&lock);
    }
    reply = std::string(verb) + " " + std::string(text);
    return true;
  }
  if (!InGame()) {
    reply = "Enter the world before tagging.";
    return true;
  }
  const auto *state = GetClientState();
  if (verb == "clear") {
    Spawn target;
    if (state && Snapshot(state->GetTarget(), target)) {
      AcquireSRWLockExclusive(&lock);
      tags.Clear(target.id);
      ReleaseSRWLockExclusive(&lock);
      refreshName(target.object, true);
    } else
      ResetTags();
    reply = "Nameplate tags cleared.";
    return true;
  }
  if (verb == "target" && !text.empty()) {
    Spawn player, best;
    float bestDistance = std::numeric_limits<float>::max();
    if (state && Snapshot(state->GetLocalPlayer(), player))
      for (const auto &[id, entry] : Entries()) {
        Spawn spawn;
        if (!Lookup(id, spawn) || entry.name != NormalizeTagName(spawn.name))
          continue;
        bool match = false;
        std::size_t pos = 0;
        do {
          auto end = entry.text.find(" | ", pos);
          if (std::string_view(entry.text)
                  .substr(pos, end == entry.text.npos ? end : end - pos) ==
              text)
            match = true;
          if (end == entry.text.npos)
            break;
          pos = end + 3;
        } while (pos < entry.text.size());
        const float distance = (spawn.x - player.x) * (spawn.x - player.x) +
                               (spawn.y - player.y) * (spawn.y - player.y);
        if (match && distance < bestDistance) {
          best = spawn;
          bestDistance = distance;
        }
      }
    auto targetSlot = ResolvePreferredVA(module, addresses::Target.preferredVA,
                                         sizeof(void *));
    if (targetSlot)
      InterlockedExchangePointer(reinterpret_cast<PVOID volatile *>(targetSlot),
                                 best.object);
    if (!best.object)
      reply = "No matching tag target found.";
    return true;
  }
  if (verb == "join" || verb == "channel") {
    const std::string name = text.empty() ? ChannelName() : std::string(text);
    if (!IsTagChannelName(name)) {
      reply = "Usage: /tag join Gt<name>";
      return true;
    }
    if (verb == "channel" && !InRaid() && !InGroup()) {
      reply = "Join a group or raid before broadcasting a tag channel.";
      return true;
    }
    SetChannelName(name);
    if (send && !ChannelNumber())
      send(context, ("/join " + name).c_str());
    if (send && verb == "channel") {
      const auto message = EncodeTagMessage(
          {std::string(TagChannelAnnouncement) + name, "0", 0});
      if (message)
        send(context, ((InRaid() ? "/rsay " : "/gsay ") + *message).c_str());
    }
    const int number = ChannelNumber();
    reply = number ? "Tag channel: " + name + " (joined as /" +
                         std::to_string(number) + ")"
                   : "Join requested for tag channel " + name +
                         ". Wait for EverQuest's join confirmation; /list "
                         "shows joined channels.";
    return true;
  }
  if (verb == "local" || verb == "gsay" || verb == "rsay" || verb == "rsgs" ||
      verb == "chat") {
    if (text.empty()) {
      reply = "Supply tag text, a shape prefix, or clear.";
      return true;
    }
    std::string route(verb);
    if (route == "rsgs")
      route = InRaid() ? "rsay" : InGroup() ? "gsay" : "local";
    if ((route == "rsay" && !InRaid()) || (route == "gsay" && !InGroup())) {
      reply = "Join a group or raid before broadcasting tags.";
      return true;
    }
    int channel = route == "chat" ? ChannelNumber() : 0;
    if (route == "chat" && !channel) {
      const auto selected = ChannelName();
      reply =
          selected.empty()
              ? "Select a tag channel with /tag join Gt<name> first."
              : "Tag channel " + selected +
                    " is not confirmed in the joined-channel list. Run /join " +
                    selected + ", then /list to check the server's response.";
      return true;
    }
    Spawn target;
    const bool clear = text == "clear";
    if (!clear && (!state || !Snapshot(state->GetTarget(), target))) {
      reply = "Select a living player or NPC with an actor to tag.";
      return true;
    }
    std::string payload(text.substr(0, 32));
    for (char &c : payload)
      if (static_cast<unsigned char>(c) < 32 ||
          static_cast<unsigned char>(c) > 126)
        c = '?';
    const std::string name = clear ? "0" : NormalizeTagName(target.name);
    const auto message =
        EncodeTagMessage({payload, name, clear ? 0 : target.id});
    const auto parsed = message ? ParseTagMessage(*message) : std::nullopt;
    if (!parsed) {
      reply =
          "Invalid tag formatting (32 characters maximum; no ' | ' in text).";
      return true;
    }
    AcquireSRWLockExclusive(&lock);
    tags.enabled = true;
    ReleaseSRWLockExclusive(&lock);
    // Applying locally gives immediate feedback; field updates are idempotent
    // on the chat echo.
    if (!Apply(*parsed)) {
      reply = "Tagging failed: target changed or is unavailable.";
      return true;
    }
    if (send && route != "local")
      send(context, ("/" + (route == "chat" ? std::to_string(channel) : route) +
                     " " + *message)
                        .c_str());
    return true;
  }
  reply = "Usage: /tag <on|off|clear>; /tag <local|gsay|rsay|rsgs|chat> "
          "<text|clear>; /tag target <text>; /tag join Gt<name>. Shapes: ^R^ "
          "^O^ ^Y^ ^G^ ^B^ ^W^ ^S^ ^P^; ^-^ hides arrow.";
  return true;
}

bool InstallTagHooks(HMODULE clientModule,
                     const ValidatedClient &clientValidation) {
  if (ready)
    return true;
  if (!clientValidation.IsValidFor(clientModule))
    return false;
  module = clientModule;
  validation = clientValidation;
  const auto spawnAddress = ResolvePreferredVA(module, 0x5996e0, 5);
  const auto nameAddress = ResolvePreferredVA(module, 0x58e2d0, 5);
  constexpr BYTE spawnBytes[] = {0x8b, 0x44, 0x24, 4, 0x50};
  // Nameplate hooks may already own this entry, so initialize tags before those
  // hooks.
  constexpr BYTE nameBytes[] = {0xb8, 0xa8, 0x26, 0, 0};
  if (!spawnAddress || !nameAddress ||
      !Readable(reinterpret_cast<void *>(spawnAddress), 5) ||
      !Readable(reinterpret_cast<void *>(nameAddress), 5) ||
      std::memcmp(reinterpret_cast<void *>(spawnAddress), spawnBytes, 5) ||
      std::memcmp(reinterpret_cast<void *>(nameAddress), nameBytes, 5))
    return false;
  getSpawn = reinterpret_cast<GetSpawnFn>(spawnAddress);
  refreshName = reinterpret_cast<NameStateFn>(nameAddress);
  constexpr BYTE stateBytes[] = {0x8b, 0x44, 0x24, 4, 0x89,
                                 0x81, 0xc8, 5,    0, 0};
  constexpr BYTE chatBytes[] = {0x64, 0xa1, 0, 0, 0, 0};
  if (!originalGameState &&
      !InstallDetour(module, validation, 0x519750, stateBytes,
                     sizeof(stateBytes),
                     reinterpret_cast<void *>(&GameStateHook),
                     reinterpret_cast<void **>(&originalGameState)))
    return false;
  if (!originalChat &&
      !InstallDetour(module, validation, 0x51f1a0, chatBytes, sizeof(chatBytes),
                     reinterpret_cast<void *>(&ChatHook),
                     reinterpret_cast<void **>(&originalChat)))
    return false;
  constexpr BYTE removeBytes[] = {0x51, 0x56, 0x8b, 0x74, 0x24, 0x0c};
  if (!originalRemoveSpawn &&
      !InstallDetour(module, validation, 0x8cf790, removeBytes,
                     sizeof(removeBytes),
                     reinterpret_cast<void *>(&RemoveSpawnHook),
                     reinterpret_cast<void **>(&originalRemoveSpawn)))
    return false;
  ready = true;
  return true;
}
} // namespace gusto::client::rof2
