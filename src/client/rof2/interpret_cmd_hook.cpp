#include "gusto/client/rof2/interpret_cmd_hook.hpp"

#include "gusto/client/rof2/tag_hooks.hpp"
#include "gusto/client/rof2/hud_overlay.hpp"
#include <string>
#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

extern "C" const char* GustoVersion();

namespace gusto::client::rof2
{
    namespace
    {
        constexpr std::uintptr_t InterpretCmdVA = 0x0051FCE0;
        constexpr std::uintptr_t DspChatVA = 0x0051F1A0;
        constexpr int UserColorDefault = 273;
        constexpr std::size_t PatchLength = 7;

        constexpr BYTE ExpectedBytes[PatchLength] =
        {
            0x6A, 0xFF, 0x68, 0x2C, 0x85, 0x97, 0x00
        };

        using InterpretCmdFunction =
            void (__thiscall *)(void*, void*, const char*);

        using DspChatFunction =
            void (__thiscall *)(void*, const char*, int, bool, bool);

        InterpretCmdFunction g_original = nullptr;
        DspChatFunction g_dspChat = nullptr;
        void* g_trampoline = nullptr;
        volatile LONG g_installed = 0;
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

            // Every thread in the first snapshot is now stopped. Check that
            // none created another process thread while the others were
            // being suspended.
            if (!SnapshotContainsOnlyKnownThreads(processId, threads))
                return false;

            for (const auto& thread : threads)
            {
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;

                if (!GetThreadContext(thread.handle, &context))
                    return false;

                const auto ip = static_cast<std::uintptr_t>(context.Eip);

                if (ip >= target && ip < target + PatchLength)
                    return false;
            }

            return true;
        }

        bool CopyCommandText(
            const char* source,
            char* destination,
            std::size_t capacity,
            std::size_t& copied)
        {
            copied = 0;

            if (!source || capacity < 2)
                return false;

            while (copied < capacity - 1)
            {
                MEMORY_BASIC_INFORMATION info{};

                const auto* current = source + copied;

                if (!VirtualQuery(current, &info, sizeof(info)) ||
                    info.State != MEM_COMMIT ||
                    (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
                    return false;

                const DWORD protection = info.Protect & 0xFF;

                if (protection != PAGE_READONLY &&
                    protection != PAGE_READWRITE &&
                    protection != PAGE_WRITECOPY &&
                    protection != PAGE_EXECUTE_READ &&
                    protection != PAGE_EXECUTE_READWRITE &&
                    protection != PAGE_EXECUTE_WRITECOPY)
                    return false;

                const auto regionEnd =
                    reinterpret_cast<std::uintptr_t>(info.BaseAddress) +
                    info.RegionSize;

                const auto currentAddress =
                    reinterpret_cast<std::uintptr_t>(current);

                if (regionEnd <= currentAddress)
                    return false;

                const std::size_t available = (std::min)(
                    capacity - 1 - copied,
                    static_cast<std::size_t>(regionEnd - currentAddress));

                const void* terminator = std::memchr(current, '\0', available);
                const std::size_t count = terminator
                    ? static_cast<const char*>(terminator) - current
                    : available;

                std::memcpy(destination + copied, current, count);
                copied += count;

                if (terminator)
                {
                    destination[copied] = '\0';
                    return true;
                }
            }

            destination[copied] = '\0';
            return false; // A truncated command must not be interpreted as a different command.
        }



        void __fastcall InterpretCmdHook(
            void* self,
            void*,
            void* who,
            const char* text)
        {

            char safeText[1024]{};
            std::size_t copied = 0;
            if (!CopyCommandText(text, safeText, sizeof(safeText), copied)) {
                if (g_original) g_original(self, who, text);
                return;
            }
            text = safeText;
            TickTags();
            std::string hudReply;
            if (HandleHudCommand(text, hudReply)) {
                if (!hudReply.empty() && g_dspChat) g_dspChat(self, hudReply.c_str(), UserColorDefault, true, true);
                return;
            }
            if (_strnicmp(text, "/tag", 4) == 0 &&
                (text[4] == '\0' || text[4] == ' ' || text[4] == '\t')) {
                struct Context { void* self; void* who; } context{self, who};
                std::string reply;
                HandleTagCommand(text + 4, &context, [](void* raw, const char* command) {
                    const auto& ctx = *static_cast<Context*>(raw);
                    if (g_original) g_original(ctx.self, ctx.who, command);
                }, reply);
                if (!reply.empty() && g_dspChat) g_dspChat(self, reply.c_str(), UserColorDefault, true, true);
                return;
            }

            if (text &&
                _strnicmp(text, "/gusto", 6) == 0 &&
                (text[6] == '\0' || text[6] == ' ' || text[6] == '\t'))
            {
                const char* command = text + 6;

                while (*command == ' ' || *command == '\t')
                    ++command;

                const auto dspChat = g_dspChat;

                if (dspChat)
                {
                    if (_stricmp(command, "version") == 0)
                    {
                        dspChat(self, GustoVersion(), UserColorDefault, true, true);
                    }
                    else
                    {
                        dspChat(self, "Usage: /gusto version", UserColorDefault, true, true);
                    }

                    return;
                }
            }

            const auto original = g_original;

            if (original)
                original(self, who, text);
        }

        bool WriteRelativeJump(
            BYTE* destination,
            const void* instructionAddress,
            const void* target)
        {
            destination[0] = 0xE9;

            const auto nextInstruction =
                reinterpret_cast<std::uintptr_t>(instructionAddress) + 5;

            const auto targetAddress =
                reinterpret_cast<std::uintptr_t>(target);

            const std::uint32_t displacement =
                static_cast<std::uint32_t>(targetAddress - nextInstruction);

            std::memcpy(destination + 1, &displacement, sizeof(displacement));
            return true;
        }
    }

    bool InstallInterpretCmdHook(
        HMODULE module,
        const ValidatedClient& validation)
    {
        if (InterlockedCompareExchange(&g_installed, 0, 0) != 0)
            return true;

        if (!validation.IsValidFor(module))
            return false;

        const auto dspChatAddress =
            ResolvePreferredVA(module, DspChatVA, 1);

        if (!dspChatAddress ||
            !IsExecutableMemory(reinterpret_cast<const void*>(dspChatAddress), 1) ||
            !IsExecutableSection(module, dspChatAddress))
            return false;

        const auto dspChat = reinterpret_cast<DspChatFunction>(dspChatAddress);

        const auto targetAddress =
            ResolvePreferredVA(module, InterpretCmdVA, PatchLength);

        if (!targetAddress ||
            !IsExecutableMemory(
                reinterpret_cast<const void*>(targetAddress), PatchLength) ||
            !IsExecutableSection(module, targetAddress))
            return false;

        auto* target = reinterpret_cast<BYTE*>(targetAddress);

        if (std::memcmp(target, ExpectedBytes, PatchLength) != 0)
            return false;

        // Build the trampoline before stopping threads or modifying the game.
        auto* trampoline = static_cast<BYTE*>(
            VirtualAlloc(nullptr, PatchLength + 5,
                         MEM_COMMIT | MEM_RESERVE,
                         PAGE_EXECUTE_READWRITE));

        if (!trampoline)
            return false;

        std::memcpy(trampoline, target, PatchLength);
        WriteRelativeJump(
            trampoline + PatchLength,
            trampoline + PatchLength,
            target + PatchLength);

        if (!FlushInstructionCache(
                GetCurrentProcess(), trampoline, PatchLength + 5))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        g_trampoline = trampoline;
        g_original = reinterpret_cast<InterpretCmdFunction>(trampoline);

        std::vector<ThreadHandle> threads;

        if (!SuspendAndCheckThreads(targetAddress, threads))
        {
            ResumeThreads(threads);
            g_original = nullptr;
            g_trampoline = nullptr;
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        DWORD oldProtection = 0;

        if (!VirtualProtect(
                target, PatchLength, PAGE_EXECUTE_READWRITE, &oldProtection))
        {
            ResumeThreads(threads);
            g_original = nullptr;
            g_trampoline = nullptr;
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        BYTE patch[PatchLength]{};
        WriteRelativeJump(
            patch,
            target,
            reinterpret_cast<const void*>(&InterpretCmdHook));
        patch[5] = 0x90;
        patch[6] = 0x90;

        g_dspChat = dspChat;
        std::memcpy(target, patch, PatchLength);

        // From this point onward the target may be redirected. Keep the
        // trampoline alive and report the hook as installed even if a
        // post-patch housekeeping operation reports an error.
        InterlockedExchange(&g_installed, 1);

        const BOOL flushed = FlushInstructionCache(
            GetCurrentProcess(), target, PatchLength);

        DWORD ignoredProtection = 0;
        const BOOL restored = VirtualProtect(
            target, PatchLength, oldProtection, &ignoredProtection);

        ResumeThreads(threads);





        return true;
    }
}
