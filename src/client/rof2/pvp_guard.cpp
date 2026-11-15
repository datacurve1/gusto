#include "gusto/client/rof2/pvp_guard.hpp"
#include "gusto/client/rof2/pvp_guard_model.hpp"
#include "gusto/client/rof2/addresses.hpp"
#include "gusto/client/rof2/globals.hpp"
#include "gusto/client/rof2/hook_utils.hpp"
#include <cstring>
#include <cstdio>
#include <string>
namespace gusto::client::rof2 {
namespace {
HMODULE game = nullptr;
ValidatedClient token;
using SendFn = void(__thiscall *)(void*, int, const char*, const char*, int);
SendFn originalSend = nullptr;
void Trace(const char* message) {
  if (auto* f=std::fopen("gusto-pvp-guard.log","a")) {
    std::fprintf(f,"%lu %s\n",GetTickCount(),message);std::fclose(f);
  }
}
bool Read(const void* object, std::size_t offset, void* output, std::size_t size) {
  const auto base = reinterpret_cast<std::uintptr_t>(object);
  if (!base || offset > UINTPTR_MAX-base || size > UINTPTR_MAX-base-offset) return false;
  auto address = base+offset, end = address+size;
  for (auto p=address;p<end;) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(p),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
        (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    const auto access=info.Protect&255;
    if (access!=PAGE_READONLY && access!=PAGE_READWRITE && access!=PAGE_WRITECOPY &&
        access!=PAGE_EXECUTE_READ && access!=PAGE_EXECUTE_READWRITE && access!=PAGE_EXECUTE_WRITECOPY) return false;
    const auto next=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+static_cast<std::uintptr_t>(info.RegionSize);
    if (next<=p) return false;
    p=(next<end?next:end);
  }
  std::memcpy(output,reinterpret_cast<void*>(address),size); return true;
}
void __fastcall SendHook(void* self, void*, int channel, const char* target, const char* text, int language) {
  if (channel==8 && RejectPvpOffCommand(self,text)) return;
  originalSend(self,channel,target,text,language);
}
}
bool RejectPvpOffCommand(void* self, const char* command) {
  if (!game || !token.IsValidFor(game)) return false;
  int state=0;
  if (!Read(self,0x5c8,&state,4) || state!=5) return false;
  auto* player=ResolvePointer(game,token,addresses::LocalPlayer,addresses::PlayerClientReferenceSize);
  BYTE area=0, enabled=0;
  if (!Read(player,0x2e1,&area,1) || !Read(player,0x349,&enabled,1)) return false;
  char zone[128]{};
  auto zoneAddress=ResolvePreferredVA(game,addresses::ZoneInfo.preferredVA+0xc0,sizeof(zone));
  const bool zoneRead=zoneAddress && Read(reinterpret_cast<void*>(zoneAddress),0,zone,sizeof(zone));
  std::size_t zoneLength=0; while (zoneLength<sizeof(zone) && zone[zoneLength]) ++zoneLength;
  if (!area && !(zoneRead && IsPvpZoneName({zone,zoneLength}))) return false;
  std::string text;
  for (std::size_t i=0;i<1024;++i) {
    char c=0; if (!Read(command,i,&c,1)) return false;
    if (!c) {
      if (!ShouldBlockPvpOff(text,true,enabled!=0)) return false;
      using ChatFn=void(__thiscall *)(void*,const char*,int,bool,bool);
      const auto chat=ResolvePreferredVA(game,0x51f1a0,1);
      if (chat) reinterpret_cast<ChatFn>(chat)(self,"Gusto: PvP cannot be turned off in this PvP zone.",0x111,true,true);
      Trace("PvP-off command blocked before submission");
      return true;
    }
    text+=c;
  }
  return false;
}
bool InstallPvpGuard(HMODULE module, const ValidatedClient& validation) {
  if (!validation.IsValidFor(module)) return false;
  if (originalSend) return game==module;
  auto handler=ResolvePreferredVA(module,0x978448,1);
  if (!handler) return false;
  BYTE expected[]={0x6a,0xff,0x68,0,0,0,0};
  auto operand=static_cast<std::uint32_t>(handler); std::memcpy(expected+3,&operand,4);
  game=module; token=validation;
  const bool installed=InstallDetour(module,validation,0x51dcd0,expected,sizeof(expected),
      reinterpret_cast<void*>(&SendHook),reinterpret_cast<void**>(&originalSend));
  Trace(installed ? "PvP send guard installed" : "ERROR: PvP send guard installation failed");
  return installed;
}
}
