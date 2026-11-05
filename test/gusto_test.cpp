#include <windows.h>

extern "C" __declspec(dllexport)
const char* GustoVersion()
{
    return "GUSTO 0.1.0 - RoF2";
}

BOOL APIENTRY DllMain(
    HMODULE module,
    DWORD reason,
    LPVOID reserved)
{
    (void)module;
    (void)reason;
    (void)reserved;
    return TRUE;
}
