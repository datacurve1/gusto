// src/client/rof2/slow_turn_keys.cpp
//
// Native Options -> Keys commands for RoF2:
//   ID 484 = Slow Turn Left   (dispatched as command 5, LEFT)
//   ID 485 = Toggle All Bags
//   ID 486 = Slow Turn Right  (dispatched as command 4, RIGHT)
//
// Every address below comes from disassembly of the target eqgame.exe.
// Nothing is written unless ALL expected bytes match (phase 1 = verify only).
//
// Requires a GNU-style toolchain (MinGW-w64 i686 / clang): global Intel-syntax asm.

#include "gusto/client/rof2/slow_turn_keys.hpp"
#include "gusto/client/rof2/compatibility.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <vector>

#if !defined(__GNUC__)
#error "slow_turn_keys.cpp needs a GNU-style compiler (global asm)."
#endif
static_assert(sizeof(void*) == 4, "RoF2 client is 32-bit");

#define GUSTO_ENTRY extern "C" __attribute__((used, force_align_arg_pointer))
#define GUSTO_THISCALL __attribute__((thiscall))

namespace {



struct ThreadRef {
  HANDLE handle = nullptr;
  DWORD id = 0;
  bool suspended = false;
};

struct PatchSite {
  uintptr_t address;
  const uint8_t* bytes;
  size_t length;
  uint8_t old[16];
};

void resume_threads(std::vector<ThreadRef>& threads) {
  for (auto it = threads.rbegin(); it != threads.rend(); ++it) {
    if (it->handle) {
      if (it->suspended) {
        ResumeThread(it->handle);
        it->suspended = false;
      }
      CloseHandle(it->handle);
    }
  }
  threads.clear();
}

bool snapshot_has_only_known_threads(DWORD pid, const std::vector<ThreadRef>& refs) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  bool valid = Thread32First(snapshot, &entry) != FALSE;
  if (valid) {
    do {
      if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == GetCurrentThreadId())
        continue;
      bool found = false;
      for (const auto& ref : refs) found = found || ref.id == entry.th32ThreadID;
      if (!found) { valid = false; break; }
    } while (Thread32Next(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return valid;
}

bool suspend_threads_outside_sites(
    const PatchSite* sites, size_t siteCount, std::vector<ThreadRef>& refs) {
  const DWORD pid = GetCurrentProcessId();
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  bool valid = Thread32First(snapshot, &entry) != FALSE;
  if (valid) {
    do {
      if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == GetCurrentThreadId())
        continue;
      HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                 THREAD_QUERY_INFORMATION, FALSE,
                                 entry.th32ThreadID);
      if (!thread) { valid = false; break; }
      refs.push_back({thread, entry.th32ThreadID, false});
    } while (Thread32Next(snapshot, &entry));
  }
  CloseHandle(snapshot);
  if (!valid) { resume_threads(refs); return false; }
  for (auto& ref : refs) {
    if (SuspendThread(ref.handle) == static_cast<DWORD>(-1)) {
      resume_threads(refs);
      return false;
    }
    ref.suspended = true;
  }
  if (!snapshot_has_only_known_threads(pid, refs)) {
    resume_threads(refs);
    return false;
  }
  for (const auto& ref : refs) {
    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL;
    if (!GetThreadContext(ref.handle, &context)) {
      resume_threads(refs);
      return false;
    }
    const uintptr_t ip = static_cast<uintptr_t>(context.Eip);
    for (size_t i = 0; i < siteCount; ++i) {
      if (ip >= sites[i].address && ip < sites[i].address + sites[i].length) {
        resume_threads(refs);
        return false;
      }
    }
  }
  return true;
}

bool threads_outside_range(const std::vector<ThreadRef>& refs,
                           uintptr_t begin, uintptr_t end) {
  for (const auto& ref : refs) {
    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL;
    if (!GetThreadContext(ref.handle, &context)) return false;
    const uintptr_t ip = static_cast<uintptr_t>(context.Eip);
    if (ip >= begin && ip < end) return false;
  }
  return true;
}

// ---- client addresses -------------------------------------------------------
constexpr uintptr_t kNameAccessor = 0x4D7190;  // const char* __cdecl(int id)
constexpr uintptr_t kNameLookup   = 0x4D71A0;  // int __cdecl(const char* name)
constexpr uintptr_t kNameTable    = 0xACBEE8;
constexpr uintptr_t kLegacyGet    = 0x513940;
constexpr uintptr_t kLegacyDelete = 0x5138A0;
constexpr uintptr_t kKeymapLoader = 0x55B100;
constexpr uintptr_t kLoadBoundCmp = 0x55B259;  // cmp edi,0x1DF in keymap load loop
constexpr uintptr_t kOptionsLoop  = 0x70A988;  // cmp ebp,0x1DF / jl  (row creation)
constexpr uintptr_t kAssignBound  = 0x70ACCB;  // cmp eax,0x1CF / jle (key assign)
constexpr uintptr_t kDispatcher   = 0x4D7230;  // void __cdecl(int id, int down, int)
constexpr uintptr_t kAutoRunTurnMask = 0x52C263;  // test [DDF720],bl; bl is speed limit
constexpr uintptr_t kCxStrAssign  = 0x805DE0;  // thiscall(CXStr*, const char*)
constexpr uintptr_t kKeymapGlobal = 0xE639B0;
constexpr uintptr_t kWindowManager = 0x15D3D00;

// MacroQuest RoF2 offsets and layouts for inventory containers.
constexpr uintptr_t kLocalPcGlobal = 0x00DD261C;      // pinstLocalPC
constexpr uintptr_t kContainerMgrGlobal = 0x00D1FD84; // pinstCContainerMgr
constexpr uintptr_t kGetCurrentProfile = 0x007DB210;
constexpr uintptr_t kOpenContainer = 0x0065CAE0;
constexpr uintptr_t kCloseContainer = 0x0065D2F0;
constexpr size_t kPcProfileManagerOffset = 0x31F0;
constexpr size_t kProfileInventoryOffset = 0x18;
constexpr size_t kInventoryItemsOffset = 0x08;
constexpr size_t kItemContentsOffset = 0xA0;
constexpr size_t kItemGlobalIndexOffset = 0x38;
constexpr size_t kItemDefinitionTypeOffset = 0x11A;
constexpr uint8_t kItemTypePack = 1;
constexpr size_t kContainerWndItemOffset = 0x220;
constexpr size_t kContainerMgrWindowsOffset = 0x04;
constexpr int kMaxContainerWindows = 36;
constexpr int kFirstBagInventorySlot = 23;
constexpr int kInventoryBagCount = 10;

constexpr uint32_t kOptionsMetaBase = 0x40C;   // OptionsWnd + 0x40C + id*8
constexpr uint32_t kCategoryOfLeft  = kOptionsMetaBase + 5 * 8 + 4;  // LEFT's category
constexpr int kFirstExtendedId = 479;
constexpr int kExtendedLimit = 487;
constexpr int kSlowLeftId = 484;
constexpr int kToggleAllBagsId = 485;
constexpr int kSlowRightId = 486;

// ---- state ------------------------------------------------------------------
bool g_held[2]       = {false, false};  // [0]=slow left, [1]=slow right
bool g_latchedSlow[2] = {false, false};
bool g_latchedMovement[8] = {};
bool g_movementHeld[8] = {};
bool g_latchMode = false;
bool g_slow_active   = false;
bool g_records_ready = false;
SRWLOCK g_speed_lock = SRWLOCK_INIT;
uintptr_t g_nameTableAddress = 0;
uintptr_t g_cxStrAssignAddress = 0;
uintptr_t g_windowManagerAddress = 0;
uintptr_t g_keymapGlobalAddress = 0;
uintptr_t g_localPcGlobalAddress = 0;
uintptr_t g_containerMgrGlobalAddress = 0;
uintptr_t g_getCurrentProfileAddress = 0;
uintptr_t g_openContainerAddress = 0;
uintptr_t g_closeContainerAddress = 0;
bool g_speedReady = false;
volatile LONG g_bagToggleKeyDown = 0;

using CxStrAssignFn = void* (GUSTO_THISCALL*)(void*, const char*);
using LegacyGetFn = int (__cdecl*)(int, int);
using LegacyDeleteFn = void (__cdecl*)(int, int);
using KeymapLoadFn = void (GUSTO_THISCALL*)(void*);
LegacyGetFn g_real_legacy_get = nullptr;
LegacyDeleteFn g_real_legacy_delete = nullptr;

// Speed patches (applied only while a slow-turn key is held).
// NOTE: the third immediate is 0x52C0DC, not 0x52C0DD (opcode b8 is at 0x52C0DB).
struct SpeedPatch {
  uintptr_t guard_va;
  uint8_t   guard;
  uintptr_t patch_va;
  uint8_t   orig;
  uint8_t   slow;
};
const SpeedPatch kSpeed[] = {
    {0x52BF45, 0xBB, 0x52BF46, 0x08, 0x04},  // mov ebx,8
    {0x52BF4A, 0x8D, 0x52BF4C, 0xFB, 0x00},  // lea edi,[ebx-5]  -> [ebx+0]
    {0x52C0DB, 0xB8, 0x52C0DC, 0x0C, 0x04},  // mov eax,0xC (left clamp)
    {0x52C3C6, 0x83, 0x52C3C8, 0x0C, 0x04},  // cmp eax,0xC (right)
    {0x52C3CB, 0xB8, 0x52C3CC, 0x0C, 0x04},  // mov eax,0xC (right clamp)
};
uintptr_t g_speedPatchAddress[sizeof(kSpeed) / sizeof(kSpeed[0])]{};
uintptr_t g_speedGuardAddress[sizeof(kSpeed) / sizeof(kSpeed[0])]{};
volatile LONG g_active = 0;

// ---- memory helpers ---------------------------------------------------------
bool is_readable_range(const void* address, size_t length) {
  if (!address || length == 0) return false;
  uintptr_t current = reinterpret_cast<uintptr_t>(address);
  if (length > UINTPTR_MAX - current) return false;
  const uintptr_t end = current + length;
  while (current < end) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
      return false;
    const DWORD protection = info.Protect & 0xFF;
    if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
        protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
      return false;
    const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (regionEnd <= current) return false;
    current = (std::min)(regionEnd, end);
  }
  return true;
}

template <typename T>
bool read_value(uintptr_t address, T& value) {
  if (!is_readable_range(reinterpret_cast<const void*>(address), sizeof(T))) return false;
  std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
  return true;
}

bool write_mem(uintptr_t addr, const void* data, size_t n) {
  DWORD old = 0;
  void* p = reinterpret_cast<void*>(addr);
  if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return false;
  std::memcpy(p, data, n);
  const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), p, n);
  DWORD ignored = 0;
  const BOOL restored = VirtualProtect(p, n, old, &ignored);
  return flushed && restored;
}

// Copies `stolen` position-independent bytes and jumps back to target+stolen.
void* make_trampoline(uintptr_t target, size_t stolen) {
  auto* p = static_cast<uint8_t*>(
      VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (!p) return nullptr;
  std::memcpy(p, reinterpret_cast<void*>(target), stolen);
  const int32_t rel = static_cast<int32_t>((target + stolen) - (reinterpret_cast<uintptr_t>(p) + stolen + 5));
  p[stolen] = 0xE9;
  std::memcpy(p + stolen + 1, &rel, 4);
  if (!FlushInstructionCache(GetCurrentProcess(), p, stolen + 5)) {
    VirtualFree(p, 0, MEM_RELEASE);
    return nullptr;
  }
  DWORD oldProtection = 0;
  if (!VirtualProtect(p, 32, PAGE_EXECUTE_READ, &oldProtection)) {
    VirtualFree(p, 0, MEM_RELEASE);
    return nullptr;
  }
  return p;
}

bool set_slow_patches(bool on) {
  AcquireSRWLockExclusive(&g_speed_lock);
  if (!g_speedReady) {
    ReleaseSRWLockExclusive(&g_speed_lock);
    return false;
  }
  if (on == g_slow_active) {
    bool consistent = true;
    for (size_t i = 0; i < sizeof(kSpeed) / sizeof(kSpeed[0]); ++i) {
      const uint8_t expected = on ? kSpeed[i].slow : kSpeed[i].orig;
      consistent = consistent &&
          *reinterpret_cast<const uint8_t*>(g_speedGuardAddress[i]) == kSpeed[i].guard &&
          *reinterpret_cast<const uint8_t*>(g_speedPatchAddress[i]) == expected;
    }
    if (consistent) {
      ReleaseSRWLockExclusive(&g_speed_lock);
      return true;
    }
  }
  // Each target is a verified single-byte immediate operand. A byte store is
  // atomic on x86, so key edges do not suspend the movement thread while it
  // may be executing the instructions that consume these immediates.
  uint8_t before[sizeof(kSpeed) / sizeof(kSpeed[0])]{};
  bool alreadyTarget = true;
  for (size_t i = 0; i < sizeof(kSpeed) / sizeof(kSpeed[0]); ++i) {
    const uint8_t current = *reinterpret_cast<const uint8_t*>(g_speedPatchAddress[i]);
    if (*reinterpret_cast<const uint8_t*>(g_speedGuardAddress[i]) != kSpeed[i].guard ||
        (current != kSpeed[i].orig && current != kSpeed[i].slow)) {
      ReleaseSRWLockExclusive(&g_speed_lock);
      return false;
    }
    before[i] = current;
    const uint8_t target = on ? kSpeed[i].slow : kSpeed[i].orig;
    alreadyTarget = alreadyTarget && current == target;
  }
  if (alreadyTarget) {
    g_slow_active = on;
    ReleaseSRWLockExclusive(&g_speed_lock);
    return true;
  }

  size_t written = 0;
  for (; written < sizeof(kSpeed) / sizeof(kSpeed[0]); ++written) {
    const uint8_t target = on ? kSpeed[written].slow : kSpeed[written].orig;
    if (before[written] != target &&
        !write_mem(g_speedPatchAddress[written], &target, 1)) {
      bool rollbackOk = true;
      for (size_t i = 0; i <= written; ++i) {
        if (*reinterpret_cast<const uint8_t*>(g_speedPatchAddress[i]) != before[i])
          rollbackOk = write_mem(g_speedPatchAddress[i], &before[i], 1) && rollbackOk;
      }

      ReleaseSRWLockExclusive(&g_speed_lock);
      return false;
    }
  }
  g_slow_active = on;
  ReleaseSRWLockExclusive(&g_speed_lock);
  return true;
}

}  // namespace

// ---- data referenced from asm ----------------------------------------------
extern "C" {
// Eight external metadata records cover IDs 479..486. Unused records have a
// zero category so no row is displayed for reserved client IDs.
__attribute__((used)) uint32_t gusto_records[16] = {};
__attribute__((used)) void* gusto_dispatch_trampoline = nullptr;
__attribute__((used)) void* gusto_lookup_trampoline = nullptr;
__attribute__((used)) void* gusto_legacy_get_trampoline = nullptr;
__attribute__((used)) void* gusto_legacy_delete_trampoline = nullptr;

void gusto_options_loop_stub();
void gusto_assign_stub();
void gusto_autorun_turn_mask_stub();
void gusto_dispatch_wrapper();
void gusto_lookup_wrapper();
void gusto_legacy_get_wrapper();
void gusto_legacy_delete_wrapper();
int gusto_native_movement_event(int command, int down);
void gusto_toggle_all_bags();
void gusto_bag_toggle_event(int down);
}

// ---- C++ entry points -------------------------------------------------------

// Replaces the native command-name accessor used by keymap save/load.
GUSTO_ENTRY const char* gusto_command_name(int id) {
  if (id == kSlowLeftId) return "SLOW_TURN_LEFT";
  if (id == kToggleAllBagsId) return "TOGGLE_ALL_BAGS";
  if (id == kSlowRightId) return "SLOW_TURN_RIGHT";
  if (id < 0 || id >= 500) return "";
  return reinterpret_cast<const char* const*>(g_nameTableAddress)[id];
}

GUSTO_ENTRY int gusto_command_lookup(const char* name) {
  if (name && std::strcmp(name, "SLOW_TURN_LEFT") == 0) return kSlowLeftId;
  if (name && std::strcmp(name, "TOGGLE_ALL_BAGS") == 0) return kToggleAllBagsId;
  if (name && std::strcmp(name, "SLOW_TURN_RIGHT") == 0) return kSlowRightId;
  return 0;
}

GUSTO_ENTRY int gusto_legacy_get(int id, int alternate) {
  if (id >= kSlowLeftId && id < kExtendedLimit)
    return 0; // custom/reserved IDs have no usable legacy key names.
  return g_real_legacy_get ? g_real_legacy_get(id, alternate) : 0;
}

GUSTO_ENTRY void gusto_legacy_delete(int id, int alternate) {
  if (id >= kSlowLeftId && id < kExtendedLimit)
    return; // the legacy command-name table has filler entries at 485+.
  if (g_real_legacy_delete)
    g_real_legacy_delete(id, alternate);
}

// Called when the Options row loop reaches the extended ID range.
GUSTO_ENTRY void gusto_prepare_records(void* window) {
  if (g_records_ready) return;
  auto assign = reinterpret_cast<CxStrAssignFn>(g_cxStrAssignAddress);
  const uint32_t category =
      *reinterpret_cast<const uint32_t*>(reinterpret_cast<uintptr_t>(window) + kCategoryOfLeft);
  char trace[128]{};
  wsprintfA(trace, "Options row hook reached; inherited category mask=0x%08lX\r\n",
            static_cast<unsigned long>(category));

  for (int id = kFirstExtendedId; id < kExtendedLimit; ++id)
    gusto_records[(id - kFirstExtendedId) * 2 + 1] = 0;
  assign(&gusto_records[(kSlowLeftId - kFirstExtendedId) * 2], "Slow Turn Left");
  gusto_records[(kSlowLeftId - kFirstExtendedId) * 2 + 1] = category;
  assign(&gusto_records[(kToggleAllBagsId - kFirstExtendedId) * 2], "Toggle All Bags");
  gusto_records[(kToggleAllBagsId - kFirstExtendedId) * 2 + 1] = 66; // UI | Commands
  assign(&gusto_records[(kSlowRightId - kFirstExtendedId) * 2], "Slow Turn Right");
  gusto_records[(kSlowRightId - kFirstExtendedId) * 2 + 1] = category;
  g_records_ready = true;
}

bool enter_or_chat_owns_input() {
  const auto* manager = reinterpret_cast<const uint8_t*>(g_windowManagerAddress);
  return *reinterpret_cast<const int*>(manager + 0xA4) == 0x14 ||
         (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
}

struct BagEntry {
  uintptr_t itemRef = 0;
  uintptr_t item = 0;
  bool open = false;
};

using GetCurrentProfileFn = void* (GUSTO_THISCALL*)(void*);
using GetItemDefinitionFn = void* (GUSTO_THISCALL*)(void*);
using OpenContainerFn = void (GUSTO_THISCALL*)(void*, const void*, const void*, bool);
using CloseContainerFn = void (GUSTO_THISCALL*)(void*, const void*, bool);

GUSTO_ENTRY void gusto_toggle_all_bags() {
  if (InterlockedCompareExchange(&g_active, 0, 0) == 0) {

    return;
  }
  if (enter_or_chat_owns_input()) {

    return;
  }
  if (!g_localPcGlobalAddress || !g_containerMgrGlobalAddress ||
      !g_getCurrentProfileAddress || !g_openContainerAddress || !g_closeContainerAddress) {

    return;
  }

  uintptr_t pc = 0;
  uintptr_t manager = 0;
  if (!read_value(g_localPcGlobalAddress, pc) || !pc ||
      !read_value(g_containerMgrGlobalAddress, manager) || !manager ||
      !is_readable_range(reinterpret_cast<const void*>(manager),
                         kContainerMgrWindowsOffset +
                             kMaxContainerWindows * sizeof(uintptr_t))) {

    return;
  }

  auto getProfile = reinterpret_cast<GetCurrentProfileFn>(g_getCurrentProfileAddress);
  auto openContainer = reinterpret_cast<OpenContainerFn>(g_openContainerAddress);
  auto closeContainer = reinterpret_cast<CloseContainerFn>(g_closeContainerAddress);
  void* profile = getProfile(reinterpret_cast<void*>(pc + kPcProfileManagerOffset));
  if (!profile || !is_readable_range(reinterpret_cast<const void*>(profile),
                                     kProfileInventoryOffset + kInventoryItemsOffset + 0x18)) {

    return;
  }

  const uintptr_t inventory = reinterpret_cast<uintptr_t>(profile) + kProfileInventoryOffset;
  uintptr_t items = 0;
  uint32_t itemCount = 0;
  if (!read_value(inventory + kInventoryItemsOffset, items) || !items ||
      !read_value(inventory + kInventoryItemsOffset + 8, itemCount) ||
      itemCount > 128 ||
      !is_readable_range(reinterpret_cast<const void*>(items),
                         static_cast<size_t>(itemCount) * sizeof(uintptr_t))) {
    char trace[128]{};
    wsprintfA(trace, "bag toggle failed: bad inventory array (count=%lu)\r\n",
              static_cast<unsigned long>(itemCount));

    return;
  }

  BagEntry bags[kInventoryBagCount]{};
  int bagCount = 0;
  for (int i = 0; i < kInventoryBagCount; ++i) {
    const uint32_t slot = static_cast<uint32_t>(kFirstBagInventorySlot + i);
    if (slot >= itemCount) continue;
    const uintptr_t itemRef = items + slot * sizeof(uintptr_t);
    uintptr_t item = 0;
    uintptr_t definition = 0;
    uint8_t itemType = 0;
    uint32_t bagCapacity = 0;
    const bool gotItem = read_value(itemRef, item);
    uintptr_t itemVtable = 0;
    uintptr_t getDefinition = 0;
    const bool gotVtable = gotItem && item && read_value(item, itemVtable) &&
        itemVtable && read_value(itemVtable + 8, getDefinition) && getDefinition;
    if (gotVtable) {
      definition = reinterpret_cast<uintptr_t>(
          reinterpret_cast<GetItemDefinitionFn>(getDefinition)(
              reinterpret_cast<void*>(item)));
    }
    const bool gotDefinition = definition != 0;
    const bool gotType = gotDefinition && definition &&
        read_value(definition + kItemDefinitionTypeOffset, itemType);
    const bool gotContents = gotItem && item &&
        read_value(item + kItemContentsOffset, bagCapacity);
    if (item) {
      char slotTrace[144]{};
      wsprintfA(slotTrace,
                "inventory slot %lu item=%08lX def=%08lX type=%u bag-capacity=%lu read=%u/%u/%u\r\n",
                static_cast<unsigned long>(slot), static_cast<unsigned long>(item),
                static_cast<unsigned long>(definition), static_cast<unsigned>(itemType),
                static_cast<unsigned long>(bagCapacity), gotItem, gotType, gotContents);

    }
    // RoF2's virtual GetItemDefinition accessor is at vtable + 0x08 (confirmed
    // by the native accessor at 0x7AFFE0); direct legacy definition fields are
    // null for the inventory objects observed in this client. MacroQuest's
    // ItemDefinition::Type == ITEMTYPE_PACK identifies actual bags.
    if (!gotItem || !item || !gotDefinition || !definition || !gotType ||
        itemType != kItemTypePack || !gotContents || bagCapacity <= 1)
      continue;
    bags[bagCount++] = {itemRef, item, false};
  }
  if (!bagCount) {
    char trace[160]{};
    wsprintfA(trace,
              "bag toggle found no bags (inventory slots=%lu, first bag slot=%d)\r\n",
              static_cast<unsigned long>(itemCount), kFirstBagInventorySlot);

    return;
  }

  for (int i = 0; i < kMaxContainerWindows; ++i) {
    uintptr_t window = 0;
    if (!read_value(manager + kContainerMgrWindowsOffset +
                        static_cast<uintptr_t>(i) * sizeof(uintptr_t), window) ||
        !window || !is_readable_range(reinterpret_cast<const void*>(window + kContainerWndItemOffset),
                                      sizeof(uintptr_t)))
      continue;
    uintptr_t openItem = 0;
    if (!read_value(window + kContainerWndItemOffset, openItem) || !openItem) continue;
    for (int bag = 0; bag < bagCount; ++bag)
      bags[bag].open = bags[bag].open || bags[bag].item == openItem;
  }

  bool allOpen = true;
  int openCount = 0;
  for (int i = 0; i < bagCount; ++i) allOpen = allOpen && bags[i].open;
  for (int i = 0; i < bagCount; ++i) openCount += bags[i].open ? 1 : 0;
  char trace[128]{};
  wsprintfA(trace, "bag toggle found %d bags; %d currently open\r\n", bagCount, openCount);


  if (allOpen) {
    for (int i = 0; i < bagCount; ++i)
      closeContainer(reinterpret_cast<void*>(manager),
                     reinterpret_cast<const void*>(bags[i].itemRef), true);

    return;
  }

  for (int i = 0; i < bagCount; ++i) {
    if (bags[i].open) continue;
    if (!is_readable_range(reinterpret_cast<const void*>(bags[i].item + kItemGlobalIndexOffset), 12))
      continue;
    openContainer(reinterpret_cast<void*>(manager),
                  reinterpret_cast<const void*>(bags[i].itemRef),
                  reinterpret_cast<const void*>(bags[i].item + kItemGlobalIndexOffset), true);
  }

}

GUSTO_ENTRY void gusto_bag_toggle_event(int down) {
  if (!down) {
    InterlockedExchange(&g_bagToggleKeyDown, 0);
    return;
  }
  if (InterlockedExchange(&g_bagToggleKeyDown, 1) == 0) {

    gusto_toggle_all_bags();
  }
}

void release_latched_inputs() {
  bool release[8]{};
  bool releasedSlow = false;
  g_latchMode = false;
  for (int command = 0; command < 8; ++command) {
    if (g_latchedMovement[command]) {
      release[command] = true;
      g_latchedMovement[command] = false;
      g_movementHeld[command] = false;
    }
  }
  for (int index = 0; index < 2; ++index) {
    if (!g_latchedSlow[index])
      continue;
    release[index == 0 ? 5 : 4] = true;
    g_latchedSlow[index] = false;
    g_held[index] = false;
    releasedSlow = true;
  }
  if (!releasedSlow) {
    bool anyMovement = false;
    for (bool command : release) anyMovement = anyMovement || command;
    if (!anyMovement) return;
  }
  if (releasedSlow)
    set_slow_patches(g_held[0] || g_held[1]);
  auto dispatch = reinterpret_cast<void (__cdecl*)(int, int, int)>(
      gusto_dispatch_trampoline);
  if (dispatch) {
    for (int command = 0; command < 8; ++command) {
      if (release[command])
        dispatch(command, 0, 0);
    }
  }

}

GUSTO_ENTRY int gusto_native_movement_event(int command, int down) {
  if (command < 0 || command >= 8)
    return 0;
  // RoF2 command 0 is AUTORUN. In this setup Enter is bound to it, so it is
  // the lock action that preserves the current movement combination.
  if (command == 0) {
    if (down) {
      bool movementHeld = g_held[0] || g_held[1];
      for (int i = 1; i < 8; ++i)
        movementHeld = movementHeld || g_movementHeld[i];
      if (movementHeld) {
        g_latchMode = true;

      }
    }
    return 0;
  }
  if (down) {
    if (enter_or_chat_owns_input()) {

      return 1;
    }
    release_latched_inputs();
    g_movementHeld[command] = true;
    return 0;
  }
  if (g_latchedMovement[command])
    return 1;
  if (g_movementHeld[command] &&
      (g_latchMode || enter_or_chat_owns_input())) {
    g_latchedMovement[command] = true;

    return 1;
  }
  g_movementHeld[command] = false;
  return 0;
}

// Called from the dispatcher wrapper. which: 0 = left (484), 1 = right (486).
GUSTO_ENTRY int gusto_slow_turn_event(int which, int down) {
  if (InterlockedCompareExchange(&g_active, 0, 0) == 0)
    return 0;
  const int index = which & 1;
  const bool wasHeld = g_held[index];
  const bool isDown = down != 0;
  if (isDown) {
    if (enter_or_chat_owns_input())
      return 0;
    if (g_latchMode || g_latchedSlow[0] || g_latchedSlow[1] ||
        std::any_of(std::begin(g_latchedMovement), std::end(g_latchedMovement),
                    [](bool value) { return value; }))
      release_latched_inputs();
    g_latchedSlow[index] = false;
  } else if (g_latchedSlow[index]) {
    return 0;
  } else if (g_held[index] &&
             (g_latchMode || enter_or_chat_owns_input())) {
    g_latchedSlow[index] = true;

    return 0;
  }
  g_held[index] = isDown;
  if (!set_slow_patches(g_held[0] || g_held[1])) {
    if (isDown)
      g_held[index] = false;


    return isDown ? 0 : 1;
  }
  if (wasHeld != isDown) {

  }
  return 1;
}

// ---- asm stubs --------------------------------------------------------------
// Absolute jumps use "push imm; ret" so no relocations are needed.
asm(R"(
.intel_syntax noprefix

# Preserve the native AutoRun mask (bit 0x08). The slow-turn speed patch
# changes EBX from 8 to 4 for the turn clamp, so testing BL here would
# incorrectly enter the right-turn path when AutoRun has bit 0x04 set.
.globl _gusto_autorun_turn_mask_stub
_gusto_autorun_turn_mask_stub:
    test  byte ptr [0x00DDF720], 0x08
    jz    1f
    push  0x0052C26F
    ret
1:  push  0x0052C3E2
    ret

# Replaces "cmp ebp,0x1DF / jl 0x70A6B0" at 0x70A988.
# eax = current record pointer, [esp+0x1C] = saved pointer, esi = Options window.
.globl _gusto_options_loop_stub
_gusto_options_loop_stub:
    cmp   ebp, 0x1DF
    jl    2f
    cmp   ebp, 0x1E7
    jge   3f
    pushad
    push  esi
    call  _gusto_prepare_records
    add   esp, 4
    popad
    mov   eax, ebp
    sub   eax, 0x1DF
    lea   eax, [eax*8 + _gusto_records]
    mov   [esp+0x1C], eax
2:  push  0x70A6B0
    ret
3:  push  0x70A994
    ret

# Replaces "cmp eax,0x1CF / jle 0x70ACEE" at 0x70ACCB.
.globl _gusto_assign_stub
_gusto_assign_stub:
    cmp   eax, 0x1CF
    jle   1f
    cmp   eax, 0x1E4
    je    1f
    cmp   eax, 0x1E5
    je    1f
    cmp   eax, 0x1E6
    je    1f
    push  0x70ACD2
    ret
1:  push  0x70ACEE
    ret

# Entry of the dispatcher: cdecl(id [esp+4], down [esp+8], 0 [esp+12]).
# eax/ecx/edx are scratch at entry. 484 -> LEFT(5), 486 -> RIGHT(4).
.globl _gusto_dispatch_wrapper
_gusto_dispatch_wrapper:
    mov   eax, [esp+4]
    cmp   eax, 0x1E5
    jne   8f
    mov   eax, [esp+8]
    push  eax
    call  _gusto_bag_toggle_event
    add   esp, 4
    jmp   4f
8:
    cmp   eax, 7
    ja    0f
    mov   ecx, [esp+8]
    push  ecx
    push  eax
    call  _gusto_native_movement_event
    add   esp, 8
    test  eax, eax
    jnz   4f
0:
    mov   eax, [esp+4]
    cmp   eax, 0x1E4
    je    2f
    cmp   eax, 0x1E6
    jne   1f
    mov   eax, 1
    jmp   3f
2:  xor   eax, eax
3:
    movzx ecx, byte ptr [esp+8]
    push  ecx
    push  eax
    call  _gusto_slow_turn_event
    add   esp, 8
    test  eax, eax
    jz    4f
    mov   eax, [esp+4]
    cmp   eax, 0x1E4
    jne   5f
    mov   eax, 5
    jmp   6f
5:  mov   eax, 4
6:  mov   [esp+4], eax
    jmp   1f
4:  ret
1:  jmp   dword ptr [_gusto_dispatch_trampoline]

.globl _gusto_lookup_wrapper
_gusto_lookup_wrapper:
    mov   eax, [esp+4]
    push  eax
    call  _gusto_command_lookup
    add   esp, 4
    cmp   eax, 0
    jne   7f
    jmp   dword ptr [_gusto_lookup_trampoline]
7:  ret

.globl _gusto_legacy_get_wrapper
_gusto_legacy_get_wrapper:
    mov   eax, [esp+4]
    push  dword ptr [esp+8]
    push  eax
    call  _gusto_legacy_get
    add   esp, 8
    ret

.globl _gusto_legacy_delete_wrapper
_gusto_legacy_delete_wrapper:
    mov   eax, [esp+4]
    push  dword ptr [esp+8]
    push  eax
    call  _gusto_legacy_delete
    add   esp, 8
    ret

.att_syntax prefix
)");

// ---- installer --------------------------------------------------------------
namespace gusto::client::rof2 {

bool install_slow_turn_keys(HMODULE module, const ValidatedClient& validation) {

  if (!validation.IsValidFor(module) || !module) {

    return false;
  }

  static LONG installed = 0;
  if (InterlockedCompareExchange(&installed, 0, 0) != 0)
    return true;

  // Compatibility currently requires the original preferred image base. All
  // code sites still pass through Gusto's resolver before they are inspected.
  const uintptr_t nameAccessor = ResolvePreferredVA(module, kNameAccessor, 12);
  const uintptr_t nameLookup = ResolvePreferredVA(module, kNameLookup, 8);
  const uintptr_t legacyGet = ResolvePreferredVA(module, kLegacyGet, 6);
  const uintptr_t legacyDelete = ResolvePreferredVA(module, kLegacyDelete, 6);
  const uintptr_t keymapLoader = ResolvePreferredVA(module, kKeymapLoader, 8);
  const uintptr_t loadBound = ResolvePreferredVA(module, kLoadBoundCmp, 6);
  const uintptr_t optionsLoop = ResolvePreferredVA(module, kOptionsLoop, 12);
  const uintptr_t assignBound = ResolvePreferredVA(module, kAssignBound, 7);
  const uintptr_t dispatcher = ResolvePreferredVA(module, kDispatcher, 8);
  const uintptr_t autoRunTurnMask = ResolvePreferredVA(module, kAutoRunTurnMask, 12);
  g_nameTableAddress = ResolvePreferredVA(module, kNameTable, 500 * sizeof(uint32_t));
  g_cxStrAssignAddress = ResolvePreferredVA(module, kCxStrAssign, 10);
  g_windowManagerAddress = ResolvePreferredVA(module, kWindowManager, 0xA8);
  g_keymapGlobalAddress = ResolvePreferredVA(module, kKeymapGlobal, sizeof(void*));
  g_localPcGlobalAddress = ResolvePreferredVA(module, kLocalPcGlobal, sizeof(void*));
  g_containerMgrGlobalAddress = ResolvePreferredVA(module, kContainerMgrGlobal, sizeof(void*));
  g_getCurrentProfileAddress = ResolvePreferredVA(module, kGetCurrentProfile, 8);
  g_openContainerAddress = ResolvePreferredVA(module, kOpenContainer, 13);
  g_closeContainerAddress = ResolvePreferredVA(module, kCloseContainer, 14);
  if (!nameAccessor || !nameLookup || !legacyGet || !legacyDelete || !keymapLoader || !loadBound || !optionsLoop ||
      !assignBound || !dispatcher || !autoRunTurnMask || !g_nameTableAddress || !g_cxStrAssignAddress ||
      !g_windowManagerAddress || !g_keymapGlobalAddress || !g_localPcGlobalAddress ||
      !g_containerMgrGlobalAddress || !g_getCurrentProfileAddress || !g_openContainerAddress ||
      !g_closeContainerAddress) {

    return false;
  }

  const uint8_t expectedName[12] =
      {0x8B,0x44,0x24,0x04,0x8B,0x04,0x85,0xE8,0xBE,0xAC,0x00,0xC3};
  const uint8_t expectedLookup[8] =
      {0x53,0x56,0x8B,0x74,0x24,0x0C,0x33,0xC0};
  const uint8_t expectedLegacyGet[6] = {0x81,0xEC,0x00,0x02,0x00,0x00};
  const uint8_t expectedLegacyDelete[6] = {0x81,0xEC,0x00,0x01,0x00,0x00};
  const uint8_t expectedKeymapLoader[8] = {0x6A,0xFF,0x68,0x37,0x9B,0x97,0x00,0x64};
  const uint8_t expectedLoad[6] = {0x81,0xFF,0xDF,0x01,0x00,0x00};
  const uint8_t expectedOptions[12] =
      {0x81,0xFD,0xDF,0x01,0x00,0x00,0x0F,0x8C,0x1C,0xFD,0xFF,0xFF};
  const uint8_t expectedAssign[7] = {0x3D,0xCF,0x01,0x00,0x00,0x7E,0x1C};
  const uint8_t expectedDispatch[8] = {0x64,0xA1,0,0,0,0,0x6A,0xFF};
  const uint8_t expectedAutoRunTurnMask[12] =
      {0x84,0x1D,0x20,0xF7,0xDD,0x00,0x0F,0x84,0x73,0x01,0x00,0x00};
  const uint8_t expectedCxStrAssign[10] =
      {0x55,0x8B,0xEC,0x8B,0x45,0x08,0x56,0x57,0x8B,0xF1};
  const uint8_t expectedGetCurrentProfile[8] =
      {0x8B,0x01,0x85,0xC0,0x74,0x15,0x8B,0x49};
  const uint8_t expectedOpenContainer[13] =
      {0x6A,0xFF,0x68,0xC9,0x69,0x98,0x00,0x64,0xA1,0x00,0x00,0x00,0x00};
  const uint8_t expectedCloseContainer[14] =
      {0x83,0xEC,0x1C,0x55,0x56,0x57,0x8B,0xF9,0x8B,0x0D,0x94,0xFC,0xD1,0x00};
  if (std::memcmp(reinterpret_cast<void*>(nameAccessor), expectedName, 12) ||
      std::memcmp(reinterpret_cast<void*>(nameLookup), expectedLookup, 8) ||
      std::memcmp(reinterpret_cast<void*>(legacyGet), expectedLegacyGet, 6) ||
      std::memcmp(reinterpret_cast<void*>(legacyDelete), expectedLegacyDelete, 6) ||
      std::memcmp(reinterpret_cast<void*>(keymapLoader), expectedKeymapLoader, 8) ||
      std::memcmp(reinterpret_cast<void*>(loadBound), expectedLoad, 6) ||
      std::memcmp(reinterpret_cast<void*>(optionsLoop), expectedOptions, 12) ||
      std::memcmp(reinterpret_cast<void*>(assignBound), expectedAssign, 7) ||
      std::memcmp(reinterpret_cast<void*>(dispatcher), expectedDispatch, 8) ||
      std::memcmp(reinterpret_cast<void*>(autoRunTurnMask), expectedAutoRunTurnMask, 12) ||
      std::memcmp(reinterpret_cast<void*>(g_cxStrAssignAddress), expectedCxStrAssign, 10) ||
      std::memcmp(reinterpret_cast<void*>(g_getCurrentProfileAddress), expectedGetCurrentProfile, 8) ||
      std::memcmp(reinterpret_cast<void*>(g_openContainerAddress), expectedOpenContainer, 13) ||
      std::memcmp(reinterpret_cast<void*>(g_closeContainerAddress), expectedCloseContainer, 14)) {


    return false;
  }

  // ASI startup can occur after KeyMaps construction. Capture that state: if
  // late, reload through the native loader after extending its bound and
  // installing the custom name/legacy hooks.
  void* const keymapAtStart =
      *reinterpret_cast<void* const*>(g_keymapGlobalAddress);

  for (size_t i = 0; i < sizeof(kSpeed) / sizeof(kSpeed[0]); ++i) {
    g_speedGuardAddress[i] = ResolvePreferredVA(module, kSpeed[i].guard_va, 1);
    g_speedPatchAddress[i] = ResolvePreferredVA(module, kSpeed[i].patch_va, 1);
    if (!g_speedGuardAddress[i] || !g_speedPatchAddress[i] ||
        *reinterpret_cast<const uint8_t*>(g_speedGuardAddress[i]) != kSpeed[i].guard ||
        *reinterpret_cast<const uint8_t*>(g_speedPatchAddress[i]) != kSpeed[i].orig) {


      return false;
    }
  }
  g_speedReady = true;

  void* trampDispatch = make_trampoline(dispatcher, 8);
  void* trampLookup = make_trampoline(nameLookup, 8);
  void* trampLegacyGet = make_trampoline(legacyGet, 6);
  void* trampLegacyDelete = make_trampoline(legacyDelete, 6);
  if (!trampDispatch || !trampLookup || !trampLegacyGet || !trampLegacyDelete) {
    if (trampDispatch) VirtualFree(trampDispatch, 0, MEM_RELEASE);
    if (trampLookup) VirtualFree(trampLookup, 0, MEM_RELEASE);
    if (trampLegacyGet) VirtualFree(trampLegacyGet, 0, MEM_RELEASE);
    if (trampLegacyDelete) VirtualFree(trampLegacyDelete, 0, MEM_RELEASE);


    return false;
  }
  gusto_dispatch_trampoline = trampDispatch;
  gusto_lookup_trampoline = trampLookup;
  gusto_legacy_get_trampoline = trampLegacyGet;
  gusto_legacy_delete_trampoline = trampLegacyDelete;
  g_real_legacy_get = reinterpret_cast<LegacyGetFn>(trampLegacyGet);
  g_real_legacy_delete = reinterpret_cast<LegacyDeleteFn>(trampLegacyDelete);

  uint8_t patchName[12], patchLookup[8], patchLegacyGet[6], patchLegacyDelete[6];
  uint8_t patchOptions[12], patchAssign[7], patchDispatch[8];
  uint8_t patchAutoRunTurnMask[12];
  auto buildJump = [](uint8_t* out, size_t length, uintptr_t from, const void* target) {
    std::memset(out, 0x90, length);
    out[0] = 0xE9;
    const uint32_t rel = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(target) - (from + 5));
    std::memcpy(out + 1, &rel, 4);
  };
  buildJump(patchName, 12, nameAccessor, reinterpret_cast<void*>(&gusto_command_name));
  buildJump(patchLookup, 8, nameLookup, reinterpret_cast<void*>(&gusto_lookup_wrapper));
  buildJump(patchLegacyGet, 6, legacyGet, reinterpret_cast<void*>(&gusto_legacy_get_wrapper));
  buildJump(patchLegacyDelete, 6, legacyDelete, reinterpret_cast<void*>(&gusto_legacy_delete_wrapper));
  buildJump(patchOptions, 12, optionsLoop, reinterpret_cast<void*>(&gusto_options_loop_stub));
  buildJump(patchAssign, 7, assignBound, reinterpret_cast<void*>(&gusto_assign_stub));
  buildJump(patchDispatch, 8, dispatcher, reinterpret_cast<void*>(&gusto_dispatch_wrapper));
  buildJump(patchAutoRunTurnMask, 12, autoRunTurnMask,
            reinterpret_cast<void*>(&gusto_autorun_turn_mask_stub));
  const uint8_t extendedLimitByte = 0xE7; // 479 -> 487; upper bytes remain 0x01,0,0.
  PatchSite sites[] = {
      {nameAccessor, patchName, sizeof(patchName), {}},
      {nameLookup, patchLookup, sizeof(patchLookup), {}},
      {legacyGet, patchLegacyGet, sizeof(patchLegacyGet), {}},
      {legacyDelete, patchLegacyDelete, sizeof(patchLegacyDelete), {}},
      {loadBound + 2, &extendedLimitByte, 1, {}},
      {optionsLoop, patchOptions, sizeof(patchOptions), {}},
      {assignBound, patchAssign, sizeof(patchAssign), {}},
      {dispatcher, patchDispatch, sizeof(patchDispatch), {}},
      {autoRunTurnMask, patchAutoRunTurnMask, sizeof(patchAutoRunTurnMask), {}},
  };

  std::vector<ThreadRef> threads;
  if (!suspend_threads_outside_sites(sites, sizeof(sites) / sizeof(sites[0]), threads)) {
    gusto_dispatch_trampoline = nullptr;
    gusto_lookup_trampoline = nullptr;
    gusto_legacy_get_trampoline = nullptr;
    gusto_legacy_delete_trampoline = nullptr;
    g_real_legacy_get = nullptr;
    g_real_legacy_delete = nullptr;
    VirtualFree(trampDispatch, 0, MEM_RELEASE);
    VirtualFree(trampLookup, 0, MEM_RELEASE);
    VirtualFree(trampLegacyGet, 0, MEM_RELEASE);
    VirtualFree(trampLegacyDelete, 0, MEM_RELEASE);


    return false;
  }

  // Ensure the client did not enter KeyMaps construction between the early
  // pointer check and the thread stop. If it is already in the loader, let
  // Gusto fail closed rather than install too late for persisted bindings.
  if (!threads_outside_range(threads, kKeymapLoader, 0x0055B343) ||
      *reinterpret_cast<void* const*>(g_keymapGlobalAddress) != keymapAtStart) {
    resume_threads(threads);
    gusto_dispatch_trampoline = nullptr;
    gusto_lookup_trampoline = nullptr;
    gusto_legacy_get_trampoline = nullptr;
    gusto_legacy_delete_trampoline = nullptr;
    g_real_legacy_get = nullptr;
    g_real_legacy_delete = nullptr;
    VirtualFree(trampDispatch, 0, MEM_RELEASE);
    VirtualFree(trampLookup, 0, MEM_RELEASE);
    VirtualFree(trampLegacyGet, 0, MEM_RELEASE);
    VirtualFree(trampLegacyDelete, 0, MEM_RELEASE);


    return false;
  }

  HMODULE pinned = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(
                              reinterpret_cast<const void*>(&install_slow_turn_keys)),
                          &pinned)) {
    resume_threads(threads);
    gusto_dispatch_trampoline = nullptr;
    gusto_lookup_trampoline = nullptr;
    gusto_legacy_get_trampoline = nullptr;
    gusto_legacy_delete_trampoline = nullptr;
    g_real_legacy_get = nullptr;
    g_real_legacy_delete = nullptr;
    VirtualFree(trampDispatch, 0, MEM_RELEASE);
    VirtualFree(trampLookup, 0, MEM_RELEASE);
    VirtualFree(trampLegacyGet, 0, MEM_RELEASE);
    VirtualFree(trampLegacyDelete, 0, MEM_RELEASE);

    return false;
  }

  size_t applied = 0;
  bool rollbackComplete = true;
  for (auto& site : sites) {
    std::memcpy(site.old, reinterpret_cast<void*>(site.address), site.length);
    applied++;
    if (!write_mem(site.address, site.bytes, site.length)) {
      while (applied) {
        auto& previous = sites[--applied];
        if (!write_mem(previous.address, previous.old, previous.length)) {
          rollbackComplete = false;

        }
      }
      resume_threads(threads);
      if (!rollbackComplete) {
        // A remaining detour may still point into these trampolines and this
        // DLL. Keep them alive and leave custom dispatch disabled.
        InterlockedExchange(&g_active, 0);

        return false;
      }
      gusto_dispatch_trampoline = nullptr;
      gusto_lookup_trampoline = nullptr;
      gusto_legacy_get_trampoline = nullptr;
      gusto_legacy_delete_trampoline = nullptr;
      g_real_legacy_get = nullptr;
      g_real_legacy_delete = nullptr;
      VirtualFree(trampDispatch, 0, MEM_RELEASE);
      VirtualFree(trampLookup, 0, MEM_RELEASE);
      VirtualFree(trampLegacyGet, 0, MEM_RELEASE);
      VirtualFree(trampLegacyDelete, 0, MEM_RELEASE);


      return false;
    }
  }
  if (keymapAtStart) {

    reinterpret_cast<KeymapLoadFn>(keymapLoader)(keymapAtStart);

  }
  resume_threads(threads);

  InterlockedExchange(&g_active, 1);
  InterlockedExchange(&installed, 1);


  return true;
}

void shutdown_slow_turn_keys() {
  InterlockedExchange(&g_active, 0);
  InterlockedExchange(&g_bagToggleKeyDown, 0);
  g_held[0] = false;
  g_held[1] = false;
  if (!g_speedReady)
    return;

}

}  // namespace gusto::client::rof2
