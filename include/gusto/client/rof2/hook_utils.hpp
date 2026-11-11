#pragma once

// include/gusto/client/rof2/hook_utils.hpp

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "gusto/client/rof2/compatibility.hpp"

namespace gusto::client::rof2
{
    // Installs a 5-byte relative-jmp detour at preferredVa.
    //
    //  - expectedBytes/length: the bytes the target must start with. `length`
    //    must be 5..16 and cover WHOLE instructions that are position
    //    independent (no relative call/jmp among them), except a single 5-byte
    // entry JMP, which is relocated to preserve an existing compatible hook. They are copied into
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
    // Detour a loaded companion module by RVA, authorized by the validated
    // game executable. The caller must provide audited whole-instruction bytes.
    bool InstallModuleDetour(
        HMODULE clientModule,
        const ValidatedClient& validation,
        HMODULE codeModule,
        std::uintptr_t rva,
        const BYTE* expectedBytes,
        std::size_t length,
        const void* hook,
        void** trampolineOut);

    struct CodePatch
    {
        std::uintptr_t preferredVa;
        std::vector<BYTE> expected;
        std::vector<BYTE> replacement;
    };

    struct CodeRange
    {
        std::uintptr_t begin;
        std::uintptr_t end;
    };

    // All sites are checked before any write. Other threads stay stopped until
    // the complete patch set is published. Zero globals and guarded routines
    // prevent changing object layouts after construction has started.
    bool ApplyCodePatches(HMODULE module, const ValidatedClient& validation,
        std::span<const CodePatch> patches,
        std::span<const CodeRange> guardedRanges = {},
        std::span<const std::uintptr_t> zeroGlobals = {});

}
