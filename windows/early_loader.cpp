// RoF2 imports DirectInput8 before login. Forward it to Windows and start Gusto
// outside DllMain so server-select hooks are available before the first list.
#include <windows.h>
#include <cstdio>
#include <string>
namespace {
HMODULE proxy = nullptr, systemInput = nullptr;
INIT_ONCE systemOnce = INIT_ONCE_STATIC_INIT, gustoOnce = INIT_ONCE_STATIC_INIT;
void Trace(const char* text) {
  if (auto* f = std::fopen("gusto-loader.log", "a")) {
    std::fprintf(f, "%lu %s\n", GetTickCount(), text); std::fclose(f);
  }
}
BOOL CALLBACK LoadSystem(PINIT_ONCE, PVOID, PVOID*) {
  wchar_t path[MAX_PATH]{};
  const UINT length = GetSystemDirectoryW(path, MAX_PATH);
  if (!length || length >= MAX_PATH - 13) return FALSE;
  std::wstring name(path); name += L"\\dinput8.dll";
  systemInput = LoadLibraryW(name.c_str());
  if (systemInput == proxy) { systemInput = nullptr; return FALSE; }
  return systemInput != nullptr;
}
FARPROC SystemProc(const char* name) {
  InitOnceExecuteOnce(&systemOnce, LoadSystem, nullptr, nullptr);
  return systemInput ? GetProcAddress(systemInput, name) : nullptr;
}
BOOL CALLBACK LoadGusto(PINIT_ONCE, PVOID, PVOID*) {
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(proxy, path, MAX_PATH);
  if (!length || length >= MAX_PATH) return FALSE;
  std::wstring name(path);
  const auto slash = name.find_last_of(L"\\/");
  if (slash == name.npos) return FALSE;
  name.resize(slash + 1); name += L"GUSTO.asi";
  HMODULE gusto = LoadLibraryW(name.c_str());
  if (!gusto) { Trace("ERROR: GUSTO.asi failed to load"); return FALSE; }
  const auto init = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(gusto, "InitializeASI"));
  if (!init) { Trace("ERROR: InitializeASI export missing"); return FALSE; }
  init();
  Trace("Gusto loaded through DirectInput before login");
  return TRUE;
}
}
extern "C" HRESULT WINAPI GustoDirectInput8Create(HINSTANCE instance, DWORD version,
    const void* iid, void** output, void* outer) {
  using Fn = HRESULT(WINAPI*)(HINSTANCE,DWORD,const void*,void**,void*);
  auto fn = reinterpret_cast<Fn>(SystemProc("DirectInput8Create"));
  if (!fn) return E_FAIL;
  const HRESULT result = fn(instance,version,iid,output,outer);
  InitOnceExecuteOnce(&gustoOnce, LoadGusto, nullptr, nullptr);
  return result;
}
extern "C" HRESULT WINAPI GustoDllCanUnloadNow() {
  auto fn = reinterpret_cast<HRESULT(WINAPI*)()>(SystemProc("DllCanUnloadNow"));
  return fn ? fn() : S_FALSE;
}
extern "C" HRESULT WINAPI GustoDllGetClassObject(const void* clsid, const void* iid, void** output) {
  auto fn = reinterpret_cast<HRESULT(WINAPI*)(const void*,const void*,void**)>(SystemProc("DllGetClassObject"));
  return fn ? fn(clsid,iid,output) : E_FAIL;
}
extern "C" HRESULT WINAPI GustoDllRegisterServer() {
  auto fn = reinterpret_cast<HRESULT(WINAPI*)()>(SystemProc("DllRegisterServer"));
  return fn ? fn() : E_NOTIMPL;
}
extern "C" HRESULT WINAPI GustoDllUnregisterServer() {
  auto fn = reinterpret_cast<HRESULT(WINAPI*)()>(SystemProc("DllUnregisterServer"));
  return fn ? fn() : E_NOTIMPL;
}
extern "C" const void* WINAPI GustoGetdfDIJoystick() {
  auto fn = reinterpret_cast<const void*(WINAPI*)()>(SystemProc("GetdfDIJoystick"));
  return fn ? fn() : nullptr;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) { proxy = instance; DisableThreadLibraryCalls(instance); }
  return TRUE;
}
