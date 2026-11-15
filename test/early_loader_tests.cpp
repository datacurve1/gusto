#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cstdio>
#include <initializer_list>
int main() {
  auto dll = LoadLibraryW(L"dinput8.dll");
  if (!dll) { std::printf("FAIL proxy load: %lu\n",GetLastError()); return 1; }
  using Create = HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
  auto create = reinterpret_cast<Create>(GetProcAddress(dll,"DirectInput8Create"));
  if (!create) return 2;
  IDirectInput8A* input = nullptr;
  const auto hr = create(GetModuleHandleW(nullptr),0x0800,IID_IDirectInput8A,reinterpret_cast<void**>(&input),nullptr);
  if (FAILED(hr) || !input) { std::printf("FAIL system DirectInput forwarding: %08lx\n",hr); return 3; }
  input->Release();
  auto gusto = GetModuleHandleW(L"GUSTO.asi");
  if (!gusto || !GetProcAddress(gusto,"InitializeASI") || !GetProcAddress(gusto,"GustoVersion")) {
    std::puts("FAIL Gusto not loaded by DirectInput startup"); return 4;
  }
  for (auto name : {"DllCanUnloadNow","DllGetClassObject","DllRegisterServer","DllUnregisterServer","GetdfDIJoystick"})
    if (!GetProcAddress(dll,name)) return 5;
  std::puts("PASS native DirectInput forwarding and early Gusto loading before login/world startup");
}
