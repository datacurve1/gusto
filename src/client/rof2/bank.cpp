#include "gusto/client/rof2/bank.hpp"

#include <array>
#include <cstring>

namespace gusto::client::rof2
{
    namespace
    {
        bool active = false;
        const char* failure = "not installed";

        void Branch(BYTE* output, BYTE opcode, std::uintptr_t instruction, std::uintptr_t target)
        {
            output[0] = opcode;
            auto displacement = static_cast<std::uint32_t>(target - instruction - 5);
            std::memcpy(output + 1, &displacement, 4);
        }
    }

    std::vector<CodePatch> BuildBankExpansionPatches(HMODULE module, void* loop)
    {
        // Whole instructions from the May 10 2013 RoF2 executable. No absolute
        // addresses are embedded in these replacements except relocated jumps.
        std::vector<CodePatch> patches{
            // Both BankWnd and BigBankWnd use this allocator/constructor pair.
            {0x498c41, {0x68,0xc8,0x02,0,0}, {0x68,0x68,0x03,0,0}},
            {0x498c7f, {0x68,0xc8,0x02,0,0}, {0x68,0x68,0x03,0,0}},
            // Keep BankSize at +2c4; bind both XML layouts to 32 personal slots.
            {0x627ce0, {0x8d,0x0c,0xcd,0x10,0,0,0}, {0xb9,0x20,0,0,0,0x90,0x90}},
            {0x627cf1, {0x8d,0x8e,0x48,0x02,0,0}, {0x8d,0x8e,0xc8,0x02,0,0}},
            {0x62805c, {0x8d,0xae,0x48,0x02,0,0}, {0x8d,0xae,0xc8,0x02,0,0}},
            {0x6280da, {0x8d,0xae,0xac,0x02,0,0}, {0x8d,0xae,0x48,0x03,0,0}},
            {0x62814e, {0x83,0xff,0x02}, {0x83,0xff,0x08}},
            // Shared-bank show/hide routines and account slot-count query.
            {0x6274ed, {0x8d,0xb3,0xac,0x02,0,0}, {0x8d,0xb3,0x48,0x03,0,0}},
            {0x6274f3, {0xbf,0x02,0,0,0}, {0xbf,0x08,0,0,0}},
            // Disabled unsafe 0x62752a stack-breaking patch
            {0x6275a0, {0x8d,0xbb,0xac,0x02,0,0}, {0x8d,0xbb,0x48,0x03,0,0}},
            {0x6275cb, {0x83,0xfe,0x02}, {0x83,0xfe,0x08}},
            // PostDraw consumes the same relocated pointer arrays.
            {0x628664, {0x8d,0xb3,0x48,0x02,0,0}, {0x8d,0xb3,0xc8,0x02,0,0}},
            {0x6286c6, {0x8d,0xb4,0x8b,0x48,0x02,0,0}, {0x8d,0xb4,0x8b,0xc8,0x02,0,0}},
            {0x6287cf, {0x8d,0xb3,0xac,0x02,0,0}, {0x8d,0xb3,0x48,0x03,0,0}},
            // Stop special two-slot hover positioning from moving added slots.
            {0x628847, {0x83,0x7c,0x24,0x18,0x02}, {0x83,0x7c,0x24,0x18,0x08}},
            // Fresh ItemContainers initialize both logical size and bookkeeping.
            {0x7cce99, {0x6a,0x18}, {0x6a,0x20}},
            {0x7ccea9, {0x6a,0x02}, {0x6a,0x08}},
            // Personal-bank remove/clear lifecycle loops.
            {0x7cc7b4, {0x83,0xf8,0x18}, {0x83,0xf8,0x20}},
            {0x7cc8d0, {0x83,0xff,0x18}, {0x83,0xff,0x20}},
            // Legacy bag-slot decoding: bases stay 2032 and 2532, 10 per bag.
            {0x694208, {0x3d,0xe0,0x08,0,0}, {0x3d,0x30,0x09,0,0}},
            {0x694241, {0x3d,0xf8,0x09,0,0}, {0x3d,0x34,0x0a,0,0}},
            // Shared-bank refresh loops in CInvSlotMgr.
            {0x698bc3, {0x83,0xfe,0x02}, {0x83,0xfe,0x08}},
            {0x698d39, {0x83,0xff,0x02}, {0x83,0xff,0x08}},
            // Counts used by lookup/movement while BankWnd is unavailable.
            {0x694150, {0xb8,0x10,0,0,0}, {0xb8,0x20,0,0,0}},
            {0x694a63, {0x8d,0x41,0x0f}, {0x8d,0x41,0x1f}},
            {0x698e1a, {0xbf,0x10,0,0,0}, {0xbf,0x20,0,0,0}},
        };
        // The original comparison counts two iterations of a 20-pixel tooltip
        // offset. Compare the existing slot counter instead, without overflowing
        // an x86 sign-extended imm8 or changing any stack offsets.
        CodePatch hover{0x6288bd,
            {0x83,0xfd,0x28,0x0f,0x8c,0x0f,0xff,0xff,0xff},
            std::vector<BYTE>(9, 0x90)};
        Branch(hover.replacement.data(), 0xe9,
            reinterpret_cast<std::uintptr_t>(module) + hover.preferredVa - PreferredImageBase,
            reinterpret_cast<std::uintptr_t>(loop));
        patches.push_back(std::move(hover));
        // Older custom skins can omit added controls. Skip null children in
        // the native show/hide loops instead of dereferencing a missing slot.
        for (auto patch : std::array<CodePatch, 3>{{
            {0x627500, {0x8b,0x0e,0x8b,0x11,0x8b,0x82,0x94,0,0,0}, std::vector<BYTE>(10,0x90)},
            {0x6275af, {0x8b,0x0f,0x8b,0x01,0x8b,0x90,0x94,0,0,0}, std::vector<BYTE>(10,0x90)},
            {0x6275bb, {0x8b,0x0f,0x8b,0x01,0x8b,0x90,0x90,0,0,0}, std::vector<BYTE>(10,0x90)}
        }}) {
            const auto offset = patch.preferredVa == 0x627500 ? 16 : patch.preferredVa == 0x6275af ? 48 : 80;
            Branch(patch.replacement.data(), 0xe9,
                reinterpret_cast<std::uintptr_t>(module) + patch.preferredVa - PreferredImageBase,
                reinterpret_cast<std::uintptr_t>(loop) + offset);
            patches.push_back(std::move(patch));
        }
        return patches;
    }

    bool InstallBankExpansion(HMODULE module, const ValidatedClient& validation)
    {
        if (active) return validation.IsValidFor(module);
        failure = "client code or startup state did not match; bank expansion disabled";
        if (!validation.IsValidFor(module)) return false;
        auto begin = ResolvePreferredVA(module, 0x6287d5, 1);
        auto end = ResolvePreferredVA(module, 0x6288c6, 1);
        if (!begin || !end) return false;
        auto* thunk = static_cast<BYTE*>(VirtualAlloc(nullptr, 128,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!thunk) return false;
        const BYTE comparison[]{0x83,0x7c,0x24,0x18,0x08,0x0f,0x8c};
        std::memcpy(thunk, comparison, sizeof(comparison));
        auto taken = static_cast<std::uint32_t>(begin - reinterpret_cast<std::uintptr_t>(thunk) - 11);
        std::memcpy(thunk + 7, &taken, 4);
        Branch(thunk + 11, 0xe9, reinterpret_cast<std::uintptr_t>(thunk) + 11, end);
        // Three null-safe versions of the displaced shared-slot vtable loads.
        const std::array<std::uintptr_t, 3> sites{0x627500,0x6275af,0x6275bb};
        const std::array<std::uintptr_t, 3> skips{0x62750c,0x6275c7,0x6275c7};
        for (std::size_t i = 0; i < sites.size(); ++i) {
            auto* code = thunk + 16 + i * 32;
            auto original = ResolvePreferredVA(module, sites[i], 10);
            auto skip = ResolvePreferredVA(module, skips[i], 1);
            if (!original || !skip) { VirtualFree(thunk, 0, MEM_RELEASE); return false; }
            std::memcpy(code, reinterpret_cast<void*>(original), 2);
            const BYTE check[]{0x85,0xc9,0x0f,0x84};
            std::memcpy(code + 2, check, 4);
            auto displacement = static_cast<std::uint32_t>(skip - reinterpret_cast<std::uintptr_t>(code) - 10);
            std::memcpy(code + 6, &displacement, 4);
            std::memcpy(code + 10, reinterpret_cast<void*>(original + 2), 8);
            Branch(code + 18, 0xe9, reinterpret_cast<std::uintptr_t>(code) + 18, original + 10);
        }
        DWORD old = 0;
        if (!VirtualProtect(thunk, 128, PAGE_EXECUTE_READ, &old) ||
            !FlushInstructionCache(GetCurrentProcess(), thunk, 128)) {
            VirtualFree(thunk, 0, MEM_RELEASE);
            return false;
        }
        auto patches = BuildBankExpansionPatches(module, thunk);
        // A late injection must never turn an already allocated 24/2 object into
        // a 32/8 object. Install only before bank or character construction.
        const std::uintptr_t globals[]{0x00d1fc90, 0x00dd261c};
        const CodeRange guards[]{{0x498b90, 0x498cd8}, {0x7cce20, 0x7cd24e}};
        if (!ApplyCodePatches(module, validation, patches, guards, globals)) {
            VirtualFree(thunk, 0, MEM_RELEASE);
            return false;
        }
        active = true;
        failure = nullptr;
        OutputDebugStringA("GUSTO: bank hooks installed (32 personal / 8 shared).\n");
        return true;
    }

    std::string BankExpansionStatus()
    {
        return active ? "Gusto bank hooks: 32 regular / 8 shared slots; server support required."
                      : std::string("Gusto bank hooks: ") + failure;
    }
}
