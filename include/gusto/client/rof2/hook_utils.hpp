#pragma once

// include/gusto/client/rof2/hook_utils.hpp

#include <windows.h>

#include <cstddef>
#include <cstdint>

#include "gusto/client/rof2/compatibility.hpp"

namespace gusto::client::rof2
{
    // Installs a 5-byte relative-jmp detour at preferredVa.
    //
    //  - expectedBytes/length: the bytes the target must start with. `length`
    //    must be 5..16 and cover WHOLE instructions that are position
    //    independent (no relative call/jmp among them). They are copied into
    //    the trampoline, which then jumps to target + length.
    //  - hook: the replacement function (same calling convention/signature).
    //  - trampolineOut: receives a callable pointer to the original function.
    //    It is assigned before the target is patched.
    //
    // Returns false without modifying the client if validation, the address
    // checks, the byte comparison, or the thread check fails.
    bool InstallDetour(
        HMODULE module,
        const ValidatedClient& validation,
        std::uintptr_t preferredVa,
        const BYTE* expectedBytes,
        std::size_t length,
        const void* hook,
        void** trampolineOut);
}
