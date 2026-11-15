#include "gusto/client/rof2/server_select.hpp"
#include "gusto/client/rof2/server_select_model.hpp"
#include "gusto/client/rof2/hook_utils.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
namespace gusto::client::rof2 {
namespace {
using ReadServerFn = int(__stdcall *)(const std::uint8_t*, void*);
ReadServerFn originalReadServer = nullptr;
using RefreshFn = void(__thiscall *)(void*);
RefreshFn originalRefresh = nullptr;
HMODULE frontendModule = nullptr;
using AssignNameFn = void*(__thiscall *)(void*, const char*);
AssignNameFn assignName = nullptr;
HANDLE worker = nullptr, stopping = nullptr;
SRWLOCK serviceLock = SRWLOCK_INIT;
HMODULE clientModule = nullptr;
ValidatedClient clientValidation;
void Trace(const char* text) {
  if (auto* f = std::fopen("gusto-server-select.log", "a")) {
    std::fprintf(f, "%lu %s\n", GetTickCount(), text); std::fclose(f);
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
int __stdcall ReadServerHook(const std::uint8_t* wire, void* record) {
  const int consumed = originalReadServer(wire, record);
  if (consumed > 0 && consumed <= 4096 && Readable(wire, consumed) && Readable(record, 0x48) &&
      PromoteVanguardServer({wire, std::size_t(consumed)}, {static_cast<std::uint8_t*>(record), 0x48})) {
    const auto name = ServerSelectWireName({wire, std::size_t(consumed)});
    const auto display = VanguardDisplayName(name);
    if (assignName && display != name) {
      // Use the frontend's CXStr assignment so reference counts stay correct.
      const std::string copy(display);
      assignName(static_cast<BYTE*>(record) + 4, copy.c_str());
      Trace("Vanguard leading [R] removed from displayed name");
    }
    Trace("Vanguard matched; categorized as Legends");
  }
  return consumed;
}
bool VanguardRecord(void* record) {
  if (!Readable(record, 8)) return false;
  void* rep = nullptr; std::memcpy(&rep, static_cast<BYTE*>(record) + 4, 4);
  if (!Readable(rep, 0x14)) return false;
  unsigned length = 0, encoding = 0;
  std::memcpy(&length, static_cast<BYTE*>(rep) + 8, 4);
  std::memcpy(&encoding, static_cast<BYTE*>(rep) + 12, 4);
  auto* text = reinterpret_cast<const char*>(rep) + 0x14;
  return length <= 512 && encoding == 0 && Readable(text, length + 1) &&
      IsVanguardServer({text, length});
}
void PinVanguardFirst() {
  auto storage = ResolvePreferredVA(frontendModule, PreferredImageBase + 0x15016c, 4);
  if (!storage) return;
  BYTE* login = nullptr; std::memcpy(&login, reinterpret_cast<void*>(storage), 4);
  if (!Readable(login, 0xd4 + 0x1c)) return;
  auto* list = login + 0xd4;
  unsigned count = 0; void* node = nullptr; void* tail = nullptr;
  std::memcpy(&count, list + 0x18, 4);
  std::memcpy(&node, list + 4, 4); std::memcpy(&tail, list + 8, 4);
  if (!count || count > 4096) return;
  std::vector<void*> nodes, records;
  void* previous = nullptr;
  for (unsigned i = 0; i < count; ++i) {
    if (!Readable(node, 12)) return;
    void* data = nullptr; void* next = nullptr; void* back = nullptr;
    std::memcpy(&data, node, 4); std::memcpy(&next, static_cast<BYTE*>(node) + 4, 4);
    std::memcpy(&back, static_cast<BYTE*>(node) + 8, 4);
    if (back != previous || !Readable(data, 0x48)) return;
    nodes.push_back(node); records.push_back(data);
    previous = node; node = next;
  }
  if (node || previous != tail) return;
  const auto original = records;
  std::stable_partition(records.begin(), records.end(), VanguardRecord);
  if (records == original) return;
  // Rotate record pointers, keeping node links, selected server identities,
  // and all non-Vanguard entries in their existing relative order.
  for (std::size_t i = 0; i < records.size(); ++i) std::memcpy(nodes[i], &records[i], 4);
  Trace("Vanguard pinned as first server entry");
}
void __fastcall RefreshHook(void* window, void*) {
  PinVanguardFirst();
  originalRefresh(window);
}
DWORD WINAPI Worker(void*) {
  bool failed = false;
  while (WaitForSingleObject(stopping, 250) == WAIT_TIMEOUT) {
    // Hold the DLL while checking/installing. The game unloads it on entering a world.
    HMODULE frontend = nullptr;
    if (!GetModuleHandleExA(0, "eqmain.dll", &frontend)) continue;
    if (!InstallServerSelectHook(clientModule, clientValidation, frontend) && !failed) {
      Trace("frontend signature/hook check failed"); failed = true;
    }
    FreeLibrary(frontend);
  }
  return 0;
}
}
bool InstallServerSelectHook(HMODULE client, const ValidatedClient& validation, HMODULE frontend) {
  if (!validation.IsValidFor(client) || !frontend) return false;
  // Audited against the RoF2 eqmain.dll: ReadServerData @1001a600,
  // AddServer @10036750 (flags+0x30, preferred=8, legends=16).
  auto* entry = reinterpret_cast<BYTE*>(ResolvePreferredVA(frontend, PreferredImageBase + 0x1a600, 5));
  auto* category = reinterpret_cast<BYTE*>(ResolvePreferredVA(frontend, PreferredImageBase + 0x3678a, 8));
  auto* setter = reinterpret_cast<BYTE*>(ResolvePreferredVA(frontend, PreferredImageBase + 0x475e0, 6));
  constexpr BYTE setterBytes[] = {0x55,0x8b,0xec,0x8b,0x45,0x08};
  if (!Readable(setter, 6) || std::memcmp(setter, setterBytes, 6)) return false;
  assignName = reinterpret_cast<AssignNameFn>(setter);
  constexpr BYTE categoryBytes[] = {0x8b,0x43,0x30,0x83,0xcf,0xff,0xa8,0x08};
  if (!Readable(entry, 5) || !Readable(category, 8) || std::memcmp(category, categoryBytes, 8)) return false;
  auto* refresh = reinterpret_cast<BYTE*>(ResolvePreferredVA(frontend, PreferredImageBase + 0x37830, 5));
  constexpr BYTE refreshBytes[] = {0x55,0x8b,0xec,0x6a,0xff};
  if (!Readable(refresh, 5)) return false;
  const auto ownJump = [](BYTE* site, const void* hook) {
    if (site[0] != 0xe9) return false;
    std::int32_t delta = 0; std::memcpy(&delta, site + 1, 4);
    return reinterpret_cast<std::uintptr_t>(site) + 5 + delta == reinterpret_cast<std::uintptr_t>(hook);
  };
  if (!ownJump(refresh, reinterpret_cast<void*>(&RefreshHook)) && std::memcmp(refresh, refreshBytes, 5)) return false;
  if (ownJump(entry, reinterpret_cast<void*>(&ReadServerHook)) &&
      ownJump(refresh, reinterpret_cast<void*>(&RefreshHook))) return true;
  frontendModule = frontend;
  if (!ownJump(entry, reinterpret_cast<void*>(&ReadServerHook))) {
    constexpr BYTE expected[] = {0x55,0x8b,0xec,0x51,0x53};
    if (!InstallModuleDetour(client, validation, frontend, 0x1a600, expected, 5,
        reinterpret_cast<void*>(&ReadServerHook), reinterpret_cast<void**>(&originalReadServer))) return false;
  }
  if (!ownJump(refresh, reinterpret_cast<void*>(&RefreshHook)) &&
      !InstallModuleDetour(client, validation, frontend, 0x37830, refreshBytes, 5,
          reinterpret_cast<void*>(&RefreshHook), reinterpret_cast<void**>(&originalRefresh))) return false;
  Trace("server-list hook installed");
  return true;
}
void StartServerSelectHooks(HMODULE client, const ValidatedClient& validation) {
  if (!validation.IsValidFor(client)) return;
  AcquireSRWLockExclusive(&serviceLock);
  if (!worker) {
    clientModule = client; clientValidation = validation;
    stopping = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopping) worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (!worker && stopping) { CloseHandle(stopping); stopping = nullptr; }
  }
  ReleaseSRWLockExclusive(&serviceLock);
}
void StopServerSelectHooks() {
  AcquireSRWLockExclusive(&serviceLock);
  if (worker) {
    SetEvent(stopping);
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker); CloseHandle(stopping);
    worker = nullptr; stopping = nullptr;
  }
  ReleaseSRWLockExclusive(&serviceLock);
}
}
