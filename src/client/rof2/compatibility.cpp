#include "gusto/client/rof2/compatibility.hpp"
#include "gusto/client/rof2/slow_turn_keys.hpp"
#include "gusto/client/rof2/initialization.hpp"

#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace gusto::client::rof2
{
    namespace
    {
        bool IsReadableRange(const void* address, std::size_t length)
        {
            if (!address || length == 0)
                return false;

            const auto start = reinterpret_cast<std::uintptr_t>(address);

            if (length > std::numeric_limits<std::uintptr_t>::max() - start)
                return false;

            const std::uintptr_t end =
                start + static_cast<std::uintptr_t>(length);
            auto current = start;

            while (current < end)
            {
                MEMORY_BASIC_INFORMATION info{};

                if (!VirtualQuery(
                    reinterpret_cast<const void*>(current),
                    &info,
                    sizeof(info)))
                    return false;

                if (info.State != MEM_COMMIT ||
                    (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                    return false;

                const DWORD protection = info.Protect & 0xFF;
                const bool readable =
                    protection == PAGE_READONLY ||
                    protection == PAGE_READWRITE ||
                    protection == PAGE_WRITECOPY ||
                    protection == PAGE_EXECUTE_READ ||
                    protection == PAGE_EXECUTE_READWRITE ||
                    protection == PAGE_EXECUTE_WRITECOPY;

                if (!readable)
                    return false;

                const std::uintptr_t regionEnd =
                    reinterpret_cast<std::uintptr_t>(info.BaseAddress) +
                    static_cast<std::uintptr_t>(info.RegionSize);

                if (regionEnd <= current)
                    return false;

                current = (std::min)(regionEnd, end);
            }

            return true;
        }

        bool CalculateFileSha256(
            const wchar_t* path,
            char output[65])
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            HANDLE file = INVALID_HANDLE_VALUE;
            std::vector<UCHAR> hashObject;
            UCHAR digest[32]{};

            bool success = false;

            do
            {
                if (BCryptOpenAlgorithmProvider(
                    &algorithm,
                    BCRYPT_SHA256_ALGORITHM,
                    nullptr,
                    0) < 0)
                    break;

                DWORD objectLength = 0;
                DWORD resultLength = 0;

                if (BCryptGetProperty(
                    algorithm,
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&objectLength),
                    sizeof(objectLength),
                    &resultLength,
                    0) < 0)
                    break;

                hashObject.resize(objectLength);

                if (BCryptCreateHash(
                    algorithm,
                    &hash,
                    hashObject.data(),
                    objectLength,
                    nullptr,
                    0,
                    0) < 0)
                    break;

                file = CreateFileW(
                    path,
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);

                if (file == INVALID_HANDLE_VALUE)
                    break;

                UCHAR buffer[65536];
                DWORD bytesRead = 0;

                bool readSucceeded = true;

                while (true)
                {
                    if (!ReadFile(file, buffer, sizeof(buffer), &bytesRead, nullptr))
                    {
                        readSucceeded = false;
                        break;
                    }

                    if (bytesRead == 0)
                        break;

                    if (BCryptHashData(hash, buffer, bytesRead, 0) < 0)
                    {
                        readSucceeded = false;
                        break;
                    }
                }

                if (!readSucceeded)
                    break;

                if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0)
                    break;

                for (unsigned i = 0; i < sizeof(digest); ++i)
                    std::sprintf(output + i * 2, "%02x", digest[i]);

                output[64] = '\0';
                success = true;

            } while (false);

            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);

            if (hash)
                BCryptDestroyHash(hash);

            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);

            return success;
        }
    }

    std::uintptr_t ResolvePreferredVA(
        HMODULE module,
        std::uintptr_t preferredVA,
        std::size_t length)
    {
        if (!module || preferredVA < PreferredImageBase)
            return 0;

        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const auto rva = preferredVA - PreferredImageBase;

        if (!IsReadableRange(
            reinterpret_cast<const void*>(base),
            sizeof(IMAGE_DOS_HEADER)))
            return 0;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return 0;

        const auto ntAddress = base + static_cast<std::uintptr_t>(dos->e_lfanew);

        if (!IsReadableRange(
            reinterpret_cast<const void*>(ntAddress),
            sizeof(IMAGE_NT_HEADERS32)))
            return 0;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(ntAddress);

        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            return 0;

        const auto imageSize = nt->OptionalHeader.SizeOfImage;

        if (rva >= imageSize || length > imageSize - rva)
            return 0;

        const auto resolved = base + rva;

        if (!IsReadableRange(
            reinterpret_cast<const void*>(resolved), length))
            return 0;

        return resolved;
    }

    bool ValidateLoadedClient(
        HMODULE module,
        ValidationReport& report,
        ValidatedClient& validatedClient)
    {
        report = {};
        validatedClient = {};

        if (!module)
        {
            report.status = ValidationStatus::NullModule;
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(module);
        report.moduleBase = base;

        if (!IsReadableRange(module, sizeof(IMAGE_DOS_HEADER)))
        {
            report.status = ValidationStatus::InvalidDosHeader;
            return false;
        }

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);

        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        {
            report.status = ValidationStatus::InvalidDosHeader;
            return false;
        }

        const auto ntAddress = base + static_cast<std::uintptr_t>(dos->e_lfanew);

        if (!IsReadableRange(
            reinterpret_cast<const void*>(ntAddress),
            sizeof(IMAGE_NT_HEADERS32)))
        {
            report.status = ValidationStatus::InvalidNtHeader;
            return false;
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(ntAddress);

        if (nt->Signature != IMAGE_NT_SIGNATURE)
        {
            report.status = ValidationStatus::InvalidNtHeader;
            return false;
        }

        if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
        {
            report.status = ValidationStatus::UnsupportedMachine;
            return false;
        }

        if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        {
            report.status = ValidationStatus::UnsupportedOptionalHeader;
            return false;
        }

        if (nt->OptionalHeader.ImageBase != PreferredImageBase)
        {
            report.status = ValidationStatus::UnexpectedImageBase;
            return false;
        }

        report.imageSize = nt->OptionalHeader.SizeOfImage;

        if (report.imageSize != ExpectedImageSize)
        {
            report.status = ValidationStatus::UnexpectedImageSize;
            return false;
        }

        const auto dateAddress = ResolvePreferredVA(
            module, VersionDateVA, sizeof(ExpectedVersionDate));

        const auto timeAddress = ResolvePreferredVA(
            module, VersionTimeVA, sizeof(ExpectedVersionTime));

        if (!dateAddress || !timeAddress)
        {
            report.status = ValidationStatus::AddressOutOfBounds;
            return false;
        }

        const auto* date = reinterpret_cast<const char*>(dateAddress);
        const auto* time = reinterpret_cast<const char*>(timeAddress);

        std::memcpy(report.actualDate, date, sizeof(ExpectedVersionDate));
        std::memcpy(report.actualTime, time, sizeof(ExpectedVersionTime));

        if (std::memcmp(date, ExpectedVersionDate, sizeof(ExpectedVersionDate)) != 0 ||
            std::memcmp(time, ExpectedVersionTime, sizeof(ExpectedVersionTime)) != 0)
        {
            report.status = ValidationStatus::VersionMismatch;
            return false;
        }

        wchar_t modulePath[MAX_PATH]{};
        const DWORD pathLength = GetModuleFileNameW(
            module, modulePath, static_cast<DWORD>(std::size(modulePath)));

        if (!pathLength || pathLength >= std::size(modulePath))
        {
            report.status = ValidationStatus::ModulePathUnavailable;
            return false;
        }

        if (!CalculateFileSha256(modulePath, report.sha256))
        {
            report.status = ValidationStatus::HashFailure;
            return false;
        }

        if (_stricmp(report.sha256, ExpectedSha256) != 0)
        {
            report.status = ValidationStatus::HashMismatch;
            return false;
        }

        report.status = ValidationStatus::Valid;
        validatedClient = ValidatedClient(module);
        return true;
    }
}

extern "C" __declspec(dllexport)
const char* GustoVersion()
{
    return "GUSTO 0.1.0 - RoF2";
}

extern "C" __declspec(dllexport)
int GustoValidateClient()
{
    gusto::client::rof2::ValidationReport report{};
    gusto::client::rof2::ValidatedClient validatedClient{};

    return gusto::client::rof2::ValidateLoadedClient(
        GetModuleHandleW(nullptr), report, validatedClient) ? 1 : 0;
}


namespace
{
    using RibProviderHandle = void*;

    volatile LONG g_initializationWorkerStarted = 0;

    void GustoTrace(const char* message)
    {
        HANDLE file = CreateFileA(
            "C:\\gusto_asi_trace.txt",
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(file, message,
                      static_cast<DWORD>(strlen(message)),
                      &written, nullptr);
            CloseHandle(file);
        }
    }

    DWORD WINAPI GustoInitializationWorker(LPVOID)
    {
        GustoTrace("worker started\r\n");
        const int result = GustoInitializeClient();
        GustoTrace(result == 1 ? "initialization succeeded\r\n" :
                   result == 0 ? "initialization failed\r\n" :
                                 "unexpected initialization status\r\n");

        HANDLE file = CreateFileA(
            "C:\\gusto_initialization.txt",
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file != INVALID_HANDLE_VALUE)
        {
            const char* message =
                result == 1 ? "GUSTO initialization succeeded" :
                result == 0 ? "GUSTO client validation failed" :
                              "GUSTO initialization returned unexpected status";

            DWORD written = 0;
            WriteFile(file, message, static_cast<DWORD>(strlen(message)),
                      &written, nullptr);
            CloseHandle(file);
        }

        return 0;
    }

    void WINAPI GustoAsiStartup()
    {
        GustoTrace("ASI_startup entered\r\n");
        OutputDebugStringA("GUSTO: ASI_startup\n");

        if (InterlockedCompareExchange(
                &g_initializationWorkerStarted, 1, 0) != 0)
        {
            return;
        }

        HANDLE thread = CreateThread(
            nullptr, 0, GustoInitializationWorker, nullptr, 0, nullptr);

        if (thread)
        {
            CloseHandle(thread);
        }
        else
        {
            InterlockedExchange(&g_initializationWorkerStarted, 0);
            OutputDebugStringA("GUSTO: failed to start initialization worker\n");
        }
    }

    void WINAPI GustoAsiShutdown()
    {
        gusto::client::rof2::shutdown_slow_turn_keys();
        OutputDebugStringA("GUSTO: ASI_shutdown\n");
    }

    struct RibEntry
    {
        DWORD type;
        const char* name;
        void* function;
        void* reserved;
    };

    using RibRegisterInterface = int (WINAPI *)(RibProviderHandle, const char*, int, const RibEntry*);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        OutputDebugStringA("GUSTO: DLL_PROCESS_ATTACH\n");

        HMODULE miles = GetModuleHandleA("mss32.dll");
        if (!miles)
        {
            OutputDebugStringA("GUSTO: mss32.dll not found\n");
            return TRUE;
        }

        auto getProviderHandle = reinterpret_cast<RibProviderHandle (WINAPI *)()>(
            GetProcAddress(miles, "_RIB_provider_library_handle@0"));
        auto registerInterface = reinterpret_cast<RibRegisterInterface>(
            GetProcAddress(miles, "RIB_register_interface"));

        if (!getProviderHandle || !registerInterface)
        {
            OutputDebugStringA("GUSTO: RIB exports not found\n");
            return TRUE;
        }

        RibEntry entries[] =
        {
            { 0, "ASI_startup", reinterpret_cast<void*>(&GustoAsiStartup), nullptr },
            { 0, "ASI_shutdown", reinterpret_cast<void*>(&GustoAsiShutdown), nullptr }
        };

        registerInterface(getProviderHandle(), "ASI codec", 2, entries);
        OutputDebugStringA("GUSTO: ASI lifecycle registered\n");
    }

    return TRUE;
}

