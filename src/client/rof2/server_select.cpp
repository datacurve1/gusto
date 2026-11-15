#include "gusto/client/rof2/server_select.hpp"
#include "gusto/client/rof2/server_select_model.hpp"
#include "gusto/client/rof2/hook_utils.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
namespace gusto::client::rof2 {
namespace {
using ReadServerFn = int(__stdcall *)(const std::uint8_t*, void*);
ReadServerFn originalReadServer = nullptr;
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
      PromoteVanguardServer({wire, std::size_t(consumed)}, {static_cast<std::uint8_t*>(record), 0x48}))
    Trace("Vanguard matched; categorized as Legends");
  return consumed;
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
  constexpr BYTE categoryBytes[] = {0x8b,0x43,0x30,0x83,0xcf,0xff,0xa8,0x08};
  if (!Readable(entry, 5) || !Readable(category, 8) || std::memcmp(category, categoryBytes, 8)) return false;
  if (entry[0] == 0xe9) {
    std::int32_t delta = 0; std::memcpy(&delta, entry + 1, 4);
    return originalReadServer && reinterpret_cast<std::uintptr_t>(entry) + 5 + delta ==
        reinterpret_cast<std::uintptr_t>(&ReadServerHook);
  }
  constexpr BYTE expected[] = {0x55,0x8b,0xec,0x51,0x53};
  if (!InstallModuleDetour(client, validation, frontend, 0x1a600, expected, 5,
      reinterpret_cast<void*>(&ReadServerHook), reinterpret_cast<void**>(&originalReadServer))) return false;
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
