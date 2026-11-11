#include "gusto/client/rof2/mercenary_ui.hpp"
#include "gusto/client/rof2/hook_utils.hpp"

#include <array>

namespace gusto::client::rof2
{
    bool InstallMercenaryUiExpansion(
        HMODULE module,
        const ValidatedClient& validation)
    {
        if (!validation.IsValidFor(module))
            return false;

        // RoF2 May 10, 2013 client.
        // Expand the mercenary management list to 100 entries.
        // Patch complete CMP instructions, not isolated bytes.
        const std::array<CodePatch, 3> patches{{
            {0x006E6E8C, {0x83, 0xFD, 0x06},
                         {0x83, 0xFD, 0x64}},
            {0x006E6EEF, {0x83, 0xFF, 0x06},
                         {0x83, 0xFF, 0x64}},
            // Mercenary capacity label: "%d of %d Max".
            {0x006E6946, {0x6A, 0x06},
                         {0x6A, 0x64}}
        }};

        return ApplyCodePatches(module, validation, patches);
    }
}
