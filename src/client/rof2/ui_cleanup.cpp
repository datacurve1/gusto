#include "gusto/client/rof2/ui_cleanup.hpp"
#include "gusto/client/rof2/ui_cleanup_policy.hpp"
#include "gusto/client/rof2/addresses.hpp"
#include "gusto/client/rof2/globals.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
namespace gusto::client::rof2 {
namespace {
HMODULE game = nullptr;
ValidatedClient token;
using StringFn = void*(__thiscall *)(void*, const char*);
using FreeStringFn = void(__thiscall *)(void*, void*);
using FindFn = void*(__thiscall *)(void*, const void*);
using RemoveFn = void(__thiscall *)(void*, void*);
StringFn makeString = nullptr;
FreeStringFn freeString = nullptr;
FindFn findChild = nullptr;
RemoveFn removePage = nullptr;
void* lastInventory = nullptr;
void* lastEqMain = nullptr;
void Trace(const char* message) {
  if (auto* f = std::fopen("gusto-ui-cleanup.log", "a")) {
    std::fprintf(f, "%lu %s\n", GetTickCount(), message); std::fclose(f);
  }
}
bool Readable(const void* data, std::size_t size) {
  auto p = reinterpret_cast<std::uintptr_t>(data);
  if (!p || !size || size > std::numeric_limits<std::uintptr_t>::max() - p) return false;
  const auto end = p + size;
  while (p < end) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(p), &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto access = info.Protect & 255;
    if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
        access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return false;
    const auto next = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + static_cast<std::uintptr_t>(info.RegionSize);
    if (next <= p) return false;
    p = (std::min)(end, next);
  }
  return true;
}
template<class T> bool Read(const void* object, std::size_t offset, T& value) {
  const auto base = reinterpret_cast<std::uintptr_t>(object);
  if (!base || offset > UINTPTR_MAX - base) return false;
  const auto* source = reinterpret_cast<const void*>(base + offset);
  if (!Readable(source, sizeof(value))) return false;
  std::memcpy(&value, source, sizeof(value)); return true;
}
void* Global(std::uintptr_t va, std::size_t size = 4) {
  return ResolvePointer(game, token, {va, addresses::Storage::PointerStorage}, size);
}
void* Child(void* parent, const char* name) {
  if (!parent || !Readable(parent, 0x1b0)) return nullptr;
  void* text = nullptr;
  makeString(&text, name);
  void* child = findChild(parent, &text);
  if (text) freeString(&text, text);
  return child;
}
void Hide(void* window) {
  BYTE shown = 0; void* vtable = nullptr; std::uintptr_t show = 0;
  if (!Read(window, 0x196, shown) || !shown || !Read(window, 0, vtable) ||
      !Read(vtable, 0xd8, show)) return;
  const auto base = reinterpret_cast<std::uintptr_t>(game);
  if (show < base || !ResolvePreferredVA(game, PreferredImageBase + show - base, 1)) return;
  using ShowFn = int(__thiscall *)(void*, bool, bool, bool);
  reinterpret_cast<ShowFn>(show)(window, false, false, true);
}
bool RemoveTab(void* tab, void* page) {
  int count = 0; void** pages = nullptr;
  if (!page || !Read(tab, 0x1e0, count) || count < 0 || count > 64 ||
      !Read(tab, 0x1e4, pages) || (count && !Readable(pages, count * sizeof(void*)))) return false;
  for (int i = 0; i < count; ++i) {
    if (pages[i] == page) {
      // RemovePage detaches the tab without destroying the native page/children.
      removePage(tab, page); Hide(page); return true;
    }
  }
  Hide(page); return false;
}
}
bool InitializeUiCleanup(HMODULE module, const ValidatedClient& validation) {
  if (!validation.IsValidFor(module)) return false;
  const auto check = [&](std::uintptr_t va, const auto& bytes) -> void* {
    auto p = ResolvePreferredVA(module, va, sizeof(bytes));
    return p && !std::memcmp(reinterpret_cast<void*>(p), bytes, sizeof(bytes)) ? reinterpret_cast<void*>(p) : nullptr;
  };
  // RoF2 CXStr ctor/release, recursive GetChildItem and CTabWnd::RemovePage.
  constexpr BYTE stringBytes[] = {0x55,0x8b,0xec,0x8b,0x45,0x08};
  constexpr BYTE freeBytes[] = {0x55,0x8b,0xec,0x6a,0xff};
  constexpr BYTE findBytes[] = {0x53,0x8b,0x5c,0x24,0x08};
  constexpr BYTE removeBytes[] = {0x51,0x55,0x56,0x8b,0xe9};
  auto make = reinterpret_cast<StringFn>(check(0x805c20, stringBytes));
  auto release = reinterpret_cast<FreeStringFn>(check(0x805b20, freeBytes));
  auto find = reinterpret_cast<FindFn>(check(0x868330, findBytes));
  auto remove = reinterpret_cast<RemoveFn>(check(0x88d650, removeBytes));
  if (!make || !release || !find || !remove) {
    Trace("ERROR: native UI function signature mismatch"); return false;
  }
  makeString = make; freeString = release; findChild = find; removePage = remove;
  game = module; token = validation;
  Trace("native UI cleanup initialized"); return true;
}
void TickUiCleanup() {
  if (!game || !token.IsValidFor(game)) return;
  int state = 0;
  if (!Read(Global(0xe67ccc), 0x5c8, state) || state != 5) return;
  // CDisplay::CreateWindows stores these roots at 498b86 and 498a13.
  auto* inventory = Global(0xd1fc8c, 0x220);
  auto* eqMain = Global(0xf713c0, 0x220);
  if (inventory) {
    bool removed = false;
    auto* tabs = Child(inventory, "IW_Subwindows");
    for (const auto* page : InventoryRemovedPages) removed |= RemoveTab(tabs, Child(inventory, page));
    for (const auto* control : InventoryHiddenControls) Hide(Child(inventory, control));
    if (inventory != lastInventory || removed) Trace("inventory tabs, crystal controls, reclaim, plot items and power source hidden");
  }
  if (eqMain) {
    for (const auto* control : EqMainHiddenControls) Hide(Child(eqMain, control));
    Hide(eqMain); // The small frame contains only the EQ/Marketplace buttons.
    if (eqMain != lastEqMain) Trace("Marketplace and EQ-menu button panel hidden");
  }
  lastInventory = inventory; lastEqMain = eqMain;
}
}
