#include "../src/client/rof2/hook_utils.cpp"
#include <cstdio>
namespace gusto::client::rof2 {std::uintptr_t ResolvePreferredVA(HMODULE,std::uintptr_t,std::size_t){return 0;}}
using namespace gusto::client::rof2;
int main(){
 auto* code=static_cast<BYTE*>(VirtualAlloc(nullptr,128,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!code)return 1;
 auto result=[&](int offset,int value){code[offset]=0xb8;std::memcpy(code+offset+1,&value,4);code[offset+5]=0xc3;};
 result(32,7);result(48,13);WriteRelativeJump(code,code,code+32);
 if(!CopyEntryInstructions(code+64,code,5))return 2;WriteRelativeJump(code+69,code+69,code+5);FlushInstructionCache(GetCurrentProcess(),code,128);
 using Fn=int(__cdecl*)();if(reinterpret_cast<Fn>(code)()!=7 || reinterpret_cast<Fn>(code+64)()!=7)return 3;
 WriteRelativeJump(code,code,code+48);FlushInstructionCache(GetCurrentProcess(),code,128);
 if(reinterpret_cast<Fn>(code)()!=13 || reinterpret_cast<Fn>(code+64)()!=7)return 4;
 VirtualFree(code,0,MEM_RELEASE);std::puts("PASS: relocated entry jump preserves the original hook chain after a new detour");return 0;
}
