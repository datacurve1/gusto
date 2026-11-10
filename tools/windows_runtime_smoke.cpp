#include <windows.h>
#include <cstdio>
#include <cstring>
#include "gusto/version.hpp"
int main(int argc,char** argv){
 if(argc!=2)return 2;
 auto module=LoadLibraryA(argv[1]);if(!module){std::printf("FAIL: LoadLibrary error %lu\n",GetLastError());return 1;}
 auto version=reinterpret_cast<const char*(__cdecl*)()>(GetProcAddress(module,"GustoVersion"));
 if(!version||std::strcmp(version(),"GUSTO " GUSTO_VERSION_STRING " - RoF2")){std::puts("FAIL: plugin version differs from VERSION");return 1;}
 std::printf("PASS: %s\n",version());
 auto initialize=GetProcAddress(module,"InitializeASI"),api=GetProcAddress(module,"GustoInitializeClient");
 if(!initialize||!api){std::puts("FAIL: ASI entry points missing");return 1;}
 reinterpret_cast<void(__cdecl*)()>(initialize)();
 auto status=reinterpret_cast<int(__cdecl*)()>(api)();
 std::printf("PASS: Windows DLL loads with system dependencies, InitializeASI exported, unsupported host rejected (status=%d)\n",status);
 FreeLibrary(module);return status==0?0:1;
}
