// Read-only Windows/Wine controller enumeration. Does not load Gusto or EQ.
#include <windows.h>
#include <xinput.h>
#include <mmsystem.h>
#include <cstdio>
#include <initializer_list>
int main(int argc,char**){
 const bool enumerateAll=argc>1;
 std::setvbuf(stdout,nullptr,_IONBF,0);std::puts("Controller probe started");
 int detected=0;
 using State=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
 HMODULE xinput=nullptr;State state=nullptr;
 for(auto name:{L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"}){
  xinput=LoadLibraryW(name);if(!xinput)continue;state=reinterpret_cast<State>(GetProcAddress(xinput,"XInputGetState"));if(state)break;FreeLibrary(xinput);xinput=nullptr;
 }
 if(state)for(DWORD slot=0;slot<4;++slot){XINPUT_STATE value{};auto result=state(slot,&value);if(result==ERROR_SUCCESS){++detected;std::printf("XInput slot %lu (Gusto slot %lu): CONNECTED buttons=0x%04x\n",slot,slot+1,value.Gamepad.wButtons);}else std::printf("XInput slot %lu: error %lu\n",slot,result);}
 auto winmm=LoadLibraryW(L"winmm.dll");
 using Caps=MMRESULT(WINAPI*)(UINT_PTR,LPJOYCAPSW,UINT);using Pos=MMRESULT(WINAPI*)(UINT,LPJOYINFOEX);
 auto caps=winmm?reinterpret_cast<Caps>(GetProcAddress(winmm,"joyGetDevCapsW")):nullptr;
 auto pos=winmm?reinterpret_cast<Pos>(GetProcAddress(winmm,"joyGetPosEx")):nullptr;
 if(caps && pos)for(UINT slot=0;slot<16;++slot){JOYCAPSW info{};auto c=caps(slot,&info,sizeof(info));if(c!=JOYERR_NOERROR)continue;
  JOYINFOEX value{};value.dwSize=sizeof(value);value.dwFlags=JOY_RETURNALL;auto result=pos(slot,&value);char name[256]{};WideCharToMultiByte(CP_UTF8,0,info.szPname,-1,name,sizeof(name),nullptr,nullptr);
  std::printf("Wine joystick ID %u (Gusto slot %u): %s; poll=%u, buttons=0x%08lx, axes=%u\n",slot,slot+1,name,result,value.dwButtons,info.wNumAxes);if(result==JOYERR_NOERROR){++detected;if(!enumerateAll)break;}
 }
 if(winmm)FreeLibrary(winmm);if(xinput)FreeLibrary(xinput);
 std::printf("Working controller backend slots: %d\n",detected);return detected?0:2;
}
