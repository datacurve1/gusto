// src/client/rof2/hook_utils.cpp
//
// Generic detour installer. The checks and thread-suspension logic are the
// ones from interpret_cmd_hook.cpp, with the patch length made a parameter.
// (A later cleanup can make interpret_cmd_hook.cpp use this too.)

#include "gusto/client/rof2/hook_utils.hpp"

#include <tlhelp32.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace gusto::client::rof2
{
    namespace
    {
        struct ThreadHandle
        {
            HANDLE handle = nullptr;
            DWORD id = 0;
            bool suspended = false;
        };

        bool IsExecutableSection(HMODULE module, std::uintptr_t address)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(module);
            const auto rva = address - base;

            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
                base + static_cast<std::uintptr_t>(dos->e_lfanew));

            const auto* section = IMAGE_FIRST_SECTION(nt);

            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            {
                const auto begin =
                    static_cast<std::uintptr_t>(section[i].VirtualAddress);

                const auto size = static_cast<std::uintptr_t>(
                    (std::max)(section[i].Misc.VirtualSize,
                               section[i].SizeOfRawData));

                if (rva >= begin && rva - begin < size)
                    return (section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            }

            return false;
        }

        bool IsExecutableMemory(const void* address, std::size_t length)
        {
            MEMORY_BASIC_INFORMATION info{};

            if (!VirtualQuery(address, &info, sizeof(info)) ||
                info.State != MEM_COMMIT ||
                (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
                return false;

            const DWORD protection = info.Protect & 0xFF;

            if (protection != PAGE_EXECUTE &&
                protection != PAGE_EXECUTE_READ &&
                protection != PAGE_EXECUTE_READWRITE &&
                protection != PAGE_EXECUTE_WRITECOPY)
                return false;

            const auto start = reinterpret_cast<std::uintptr_t>(address);
            const auto regionStart =
                reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            const auto regionEnd = regionStart + info.RegionSize;

            return start >= regionStart &&
                   start <= regionEnd &&
                   length <= regionEnd - start;
        }

        bool SnapshotContainsOnlyKnownThreads(
            DWORD processId,
            const std::vector<ThreadHandle>& threads)
        {
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

            if (snapshot == INVALID_HANDLE_VALUE)
                return false;

            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);

            bool valid = true;

            if (Thread32First(snapshot, &entry))
            {
                do
                {
                    if (entry.th32OwnerProcessID != processId ||
                        entry.th32ThreadID == GetCurrentThreadId())
                        continue;

                    const bool known = std::any_of(
                        threads.begin(), threads.end(),
                        [&](const ThreadHandle& thread)
                        {
                            return thread.id == entry.th32ThreadID;
                        });

                    if (!known)
                    {
                        valid = false;
                        break;
                    }
                }
                while (Thread32Next(snapshot, &entry));
            }
            else
            {
                valid = false;
            }

            CloseHandle(snapshot);
            return valid;
        }

        void ResumeThreads(std::vector<ThreadHandle>& threads)
        {
            for (auto it = threads.rbegin(); it != threads.rend(); ++it)
            {
                if (it->suspended)
                    ResumeThread(it->handle);

                if (it->handle)
                    CloseHandle(it->handle);
            }

            threads.clear();
        }

        bool SuspendAndCheckThreads(
            std::uintptr_t target,
            std::size_t length,
            std::vector<ThreadHandle>& threads)
        {
            const DWORD processId = GetCurrentProcessId();
            const DWORD currentThread = GetCurrentThreadId();

            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

            if (snapshot == INVALID_HANDLE_VALUE)
                return false;

            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);

            bool enumerated = Thread32First(snapshot, &entry) != FALSE;

            if (enumerated)
            {
                do
                {
                    if (entry.th32OwnerProcessID != processId ||
                        entry.th32ThreadID == currentThread)
                        continue;

                    HANDLE thread = OpenThread(
                        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                        THREAD_QUERY_INFORMATION,
                        FALSE,
                        entry.th32ThreadID);

                    if (!thread)
                    {
                        enumerated = false;
                        break;
                    }

                    threads.push_back({thread, entry.th32ThreadID, false});
                }
                while (Thread32Next(snapshot, &entry));
            }

            CloseHandle(snapshot);

            if (!enumerated)
                return false;

            for (auto& thread : threads)
            {
                if (SuspendThread(thread.handle) == static_cast<DWORD>(-1))
                    return false;

                thread.suspended = true;
            }

            if (!SnapshotContainsOnlyKnownThreads(processId, threads))
                return false;

            for (const auto& thread : threads)
            {
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;

                if (!GetThreadContext(thread.handle, &context))
                    return false;

                const auto ip = static_cast<std::uintptr_t>(context.Eip);

                if (ip >= target && ip < target + length)
                    return false;
            }

            return true;
        }

        void WriteRelativeJump(
            BYTE* destination,
            const void* instructionAddress,
            const void* target)
        {
            destination[0] = 0xE9;

            const auto nextInstruction =
                reinterpret_cast<std::uintptr_t>(instructionAddress) + 5;

            const std::uint32_t displacement = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(target) - nextInstruction);

            std::memcpy(destination + 1, &displacement, sizeof(displacement));
        }
    }

    bool InstallDetour(
        HMODULE module,
        const ValidatedClient& validation,
        std::uintptr_t preferredVa,
        const BYTE* expectedBytes,
        std::size_t length,
        const void* hook,
        void** trampolineOut)
    {
        if (!expectedBytes || !hook || !trampolineOut || length < 5 || length > 16)
            return false;

        if (!validation.IsValidFor(module))
            return false;

        const auto targetAddress = ResolvePreferredVA(module, preferredVa, length);

        if (!targetAddress ||
            !IsExecutableMemory(reinterpret_cast<const void*>(targetAddress), length) ||
            !IsExecutableSection(module, targetAddress))
            return false;

        auto* target = reinterpret_cast<BYTE*>(targetAddress);

        if (std::memcmp(target, expectedBytes, length) != 0)
            return false;

        // Build the trampoline before stopping threads or modifying the game.
        auto* trampoline = static_cast<BYTE*>(
            VirtualAlloc(nullptr, length + 5,
                         MEM_COMMIT | MEM_RESERVE,
                         PAGE_EXECUTE_READWRITE));

        if (!trampoline)
            return false;

        std::memcpy(trampoline, target, length);
        WriteRelativeJump(trampoline + length, trampoline + length, target + length);

        if (!FlushInstructionCache(GetCurrentProcess(), trampoline, length + 5))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        std::vector<ThreadHandle> threads;

        if (!SuspendAndCheckThreads(targetAddress, length, threads))
        {
            ResumeThreads(threads);
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        DWORD oldProtection = 0;

        if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &oldProtection))
        {
            ResumeThreads(threads);
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        BYTE patch[16];
        std::memset(patch, 0x90, sizeof(patch));
        WriteRelativeJump(patch, target, hook);

        // Publish the trampoline before the jump goes live.
        *trampolineOut = trampoline;
        std::memcpy(target, patch, length);

        const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), target, length);

        DWORD ignoredProtection = 0;
        const BOOL restored =
            VirtualProtect(target, length, oldProtection, &ignoredProtection);

        ResumeThreads(threads);





        return true;
    }
}
