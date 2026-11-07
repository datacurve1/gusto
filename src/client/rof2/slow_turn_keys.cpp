// src/client/rof2/slow_turn_keys.cpp
//
// Native Options -> Keys commands for RoF2:
//   ID 484 = Slow Turn Left   (dispatched as command 5, LEFT)
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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

#if !defined(__GNUC__)
#error "slow_turn_keys.cpp needs a GNU-style compiler (global asm)."
#endif
static_assert(sizeof(void*) == 4, "RoF2 client is 32-bit");

#define GUSTO_ENTRY extern "C" __attribute__((used, force_align_arg_pointer))
#define GUSTO_THISCALL __attribute__((thiscall))

namespace {

void trace_slow_turn(const char* message) {
  // Reuse the ASI trace path that is already confirmed writable in this Wine
  // setup. This also lets us distinguish the active DLL from stale copies.
  HANDLE file = CreateFileA("C:\\gusto_asi_trace.txt", FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, message, static_cast<DWORD>(std::strlen(message)), &written,
            nullptr);
  CloseHandle(file);
}

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
    if (it->suspended && ResumeThread(it->handle) == static_cast<DWORD>(-1))
      OutputDebugStringA("GUSTO: failed to resume a client thread after hook patching.\n");
    if (it->handle) CloseHandle(it->handle);
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
constexpr uintptr_t kCxStrAssign  = 0x805DE0;  // thiscall(CXStr*, const char*)
constexpr uintptr_t kKeymapGlobal = 0xE639B0;
constexpr uintptr_t kWindowManager = 0x15D3D00;

constexpr uint32_t kOptionsMetaBase = 0x40C;   // OptionsWnd + 0x40C + id*8
constexpr uint32_t kCategoryOfLeft  = kOptionsMetaBase + 5 * 8 + 4;  // LEFT's category
constexpr int kFirstExtendedId = 479;
constexpr int kExtendedLimit = 487;
constexpr int kSlowLeftId = 484;
constexpr int kSlowRightId = 486;

// ---- state ------------------------------------------------------------------
bool g_held[2]       = {false, false};  // [0]=slow left, [1]=slow right
bool g_slow_active   = false;
bool g_records_ready = false;
SRWLOCK g_speed_lock = SRWLOCK_INIT;
uintptr_t g_nameTableAddress = 0;
uintptr_t g_cxStrAssignAddress = 0;
uintptr_t g_windowManagerAddress = 0;
uintptr_t g_keymapGlobalAddress = 0;
bool g_speedReady = false;

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
  PatchSite speedSites[] = {
      {g_speedGuardAddress[0], nullptr, 5, {}},
      {g_speedGuardAddress[1], nullptr, 3, {}},
      {g_speedGuardAddress[2], nullptr, 5, {}},
      {g_speedGuardAddress[3], nullptr, 3, {}},
      {g_speedGuardAddress[4], nullptr, 5, {}},
  };
  std::vector<ThreadRef> threads;
  if (!suspend_threads_outside_sites(speedSites, 5, threads)) {
    ReleaseSRWLockExclusive(&g_speed_lock);
    return false;
  }
  uint8_t before[sizeof(kSpeed) / sizeof(kSpeed[0])]{};
  bool alreadyTarget = true;
  for (size_t i = 0; i < sizeof(kSpeed) / sizeof(kSpeed[0]); ++i) {
    const uint8_t current = *reinterpret_cast<const uint8_t*>(g_speedPatchAddress[i]);
    if (*reinterpret_cast<const uint8_t*>(g_speedGuardAddress[i]) != kSpeed[i].guard ||
        (current != kSpeed[i].orig && current != kSpeed[i].slow)) {
      resume_threads(threads);
      ReleaseSRWLockExclusive(&g_speed_lock);
      return false;
    }
    before[i] = current;
    const uint8_t target = on ? kSpeed[i].slow : kSpeed[i].orig;
    alreadyTarget = alreadyTarget && current == target;
  }
  if (alreadyTarget) {
    g_slow_active = on;
    resume_threads(threads);
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
      if (!rollbackOk)
        OutputDebugStringA("GUSTO: speed patch rollback incomplete; next transition will reconcile bytes.\n");
      resume_threads(threads);
      ReleaseSRWLockExclusive(&g_speed_lock);
      return false;
    }
  }
  resume_threads(threads);
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
void gusto_dispatch_wrapper();
void gusto_lookup_wrapper();
void gusto_legacy_get_wrapper();
void gusto_legacy_delete_wrapper();
}

// ---- C++ entry points -------------------------------------------------------

// Replaces the native command-name accessor used by keymap save/load.
GUSTO_ENTRY const char* gusto_command_name(int id) {
  if (id == kSlowLeftId) return "SLOW_TURN_LEFT";
  if (id == kSlowRightId) return "SLOW_TURN_RIGHT";
  // ID 485 has no native name pointer; the extended loader still visits it.
  if (id == 485) return "GUSTO_RESERVED_485";
  if (id < 0 || id >= 500) return "";
  return reinterpret_cast<const char* const*>(g_nameTableAddress)[id];
}

GUSTO_ENTRY int gusto_command_lookup(const char* name) {
  if (name && std::strcmp(name, "SLOW_TURN_LEFT") == 0) return kSlowLeftId;
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
  trace_slow_turn(trace);
  for (int id = kFirstExtendedId; id < kExtendedLimit; ++id)
    gusto_records[(id - kFirstExtendedId) * 2 + 1] = 0;
  assign(&gusto_records[(kSlowLeftId - kFirstExtendedId) * 2], "Slow Turn Left");
  gusto_records[(kSlowLeftId - kFirstExtendedId) * 2 + 1] = category;
  assign(&gusto_records[(kSlowRightId - kFirstExtendedId) * 2], "Slow Turn Right");
  gusto_records[(kSlowRightId - kFirstExtendedId) * 2 + 1] = category;
  g_records_ready = true;
}

// Called from the dispatcher wrapper. which: 0 = left (484), 1 = right (486).
GUSTO_ENTRY int gusto_slow_turn_event(int which, int down) {
  if (InterlockedCompareExchange(&g_active, 0, 0) == 0)
    return 0;
  const int index = which & 1;
  if (down) {
    // RoF2 CXWndManager::ManagerMode is WndManagerModeTyping (0x14) while
    // the chat edit control owns keyboard input.
    const auto* manager = reinterpret_cast<const uint8_t*>(g_windowManagerAddress);
    if (*reinterpret_cast<const int*>(manager + 0xA4) == 0x14)
      return 0;
  }
  g_held[index] = (down != 0);
  if (!set_slow_patches(g_held[0] || g_held[1]))
    OutputDebugStringA("GUSTO: slow-turn speed patch update failed; using normal movement dispatch.\n");
  return 1;
}

// ---- asm stubs --------------------------------------------------------------
// Absolute jumps use "push imm; ret" so no relocations are needed.
asm(R"(
.intel_syntax noprefix

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
  trace_slow_turn("slow-turn installer entered\r\n");
  if (!validation.IsValidFor(module) || !module) {
    trace_slow_turn("installer rejected validation token or module\r\n");
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
  g_nameTableAddress = ResolvePreferredVA(module, kNameTable, 500 * sizeof(uint32_t));
  g_cxStrAssignAddress = ResolvePreferredVA(module, kCxStrAssign, 10);
  g_windowManagerAddress = ResolvePreferredVA(module, kWindowManager, 0xA8);
  g_keymapGlobalAddress = ResolvePreferredVA(module, kKeymapGlobal, sizeof(void*));
  if (!nameAccessor || !nameLookup || !legacyGet || !legacyDelete || !keymapLoader || !loadBound || !optionsLoop ||
      !assignBound || !dispatcher || !g_nameTableAddress || !g_cxStrAssignAddress ||
      !g_windowManagerAddress || !g_keymapGlobalAddress) {
    trace_slow_turn("installer failed resolving one or more client addresses\r\n");
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
  const uint8_t expectedCxStrAssign[10] =
      {0x55,0x8B,0xEC,0x8B,0x45,0x08,0x56,0x57,0x8B,0xF1};
  if (std::memcmp(reinterpret_cast<void*>(nameAccessor), expectedName, 12) ||
      std::memcmp(reinterpret_cast<void*>(nameLookup), expectedLookup, 8) ||
      std::memcmp(reinterpret_cast<void*>(legacyGet), expectedLegacyGet, 6) ||
      std::memcmp(reinterpret_cast<void*>(legacyDelete), expectedLegacyDelete, 6) ||
      std::memcmp(reinterpret_cast<void*>(keymapLoader), expectedKeymapLoader, 8) ||
      std::memcmp(reinterpret_cast<void*>(loadBound), expectedLoad, 6) ||
      std::memcmp(reinterpret_cast<void*>(optionsLoop), expectedOptions, 12) ||
      std::memcmp(reinterpret_cast<void*>(assignBound), expectedAssign, 7) ||
      std::memcmp(reinterpret_cast<void*>(dispatcher), expectedDispatch, 8) ||
      std::memcmp(reinterpret_cast<void*>(g_cxStrAssignAddress), expectedCxStrAssign, 10)) {
    trace_slow_turn("installer rejected a hook-site signature\r\n");
    OutputDebugStringA("GUSTO: slow-turn hook signature mismatch; no sites patched.\n");
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
      trace_slow_turn("installer rejected a speed-patch signature\r\n");
      OutputDebugStringA("GUSTO: slow-turn speed patch signature mismatch.\n");
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
    trace_slow_turn("installer could not allocate hook trampolines\r\n");
    OutputDebugStringA("GUSTO: slow-turn trampoline allocation failed.\n");
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
    trace_slow_turn("installer could not suspend client threads safely\r\n");
    OutputDebugStringA("GUSTO: could not safely suspend client threads for key hooks.\n");
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
    trace_slow_turn("installer rejected startup race with KeyMaps construction\r\n");
    OutputDebugStringA("GUSTO: KeyMaps startup raced hook installation; refusing install.\n");
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
    trace_slow_turn("installer could not pin GUSTO module\r\n");
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
          OutputDebugStringA("GUSTO: slow-turn rollback write failed.\n");
        }
      }
      resume_threads(threads);
      if (!rollbackComplete) {
        // A remaining detour may still point into these trampolines and this
        // DLL. Keep them alive and leave custom dispatch disabled.
        InterlockedExchange(&g_active, 0);
        OutputDebugStringA("GUSTO: incomplete hook rollback; preserving trampoline memory.\n");
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
      OutputDebugStringA("GUSTO: slow-turn hook transaction failed and was rolled back.\n");
      trace_slow_turn("installer hook transaction failed and rolled back\r\n");
      return false;
    }
  }
  if (keymapAtStart) {
    trace_slow_turn("late-start: reloading KeyMaps through native loader\r\n");
    reinterpret_cast<KeymapLoadFn>(keymapLoader)(keymapAtStart);
    trace_slow_turn("late-start: native KeyMaps reload returned\r\n");
  }
  resume_threads(threads);

  InterlockedExchange(&g_active, 1);
  InterlockedExchange(&installed, 1);
  trace_slow_turn("slow-turn hooks installed for command IDs 484 and 486\r\n");
  OutputDebugStringA("GUSTO: slow-turn key hooks installed (IDs 484, 486).\n");
  return true;
}

void shutdown_slow_turn_keys() {
  InterlockedExchange(&g_active, 0);
  g_held[0] = false;
  g_held[1] = false;
  if (!g_speedReady)
    return;
  if (!set_slow_patches(false))
    OutputDebugStringA("GUSTO: unable to restore all slow-turn speed bytes at shutdown.\n");
}

}  // namespace gusto::client::rof2
