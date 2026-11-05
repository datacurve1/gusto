#include <windows.h>
#include <iostream>

int main()
{
    HMODULE dll = LoadLibraryA("gusto_test.dll");

    if (!dll) {
        std::cerr << "FAILED: LoadLibrary error "
                  << GetLastError() << '\n';
        return 1;
    }

    using VersionFunction = const char* (*)();

    auto version = reinterpret_cast<VersionFunction>(
        GetProcAddress(dll, "GustoVersion"));

    if (!version) {
        std::cerr << "FAILED: Export not found\n";
        FreeLibrary(dll);
        return 2;
    }

    std::cout << "SUCCESS: " << version() << '\n';

    FreeLibrary(dll);
    return 0;
}
