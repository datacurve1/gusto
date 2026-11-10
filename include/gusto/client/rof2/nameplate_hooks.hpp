#pragma once

// include/gusto/client/rof2/nameplate_hooks.hpp

#include "gusto/client/rof2/compatibility.hpp"

namespace gusto::client::rof2
{
    struct NameplateOptions {
        bool conColors = true, pinkTarget = true, brackets = true, attackFlash = true;
        float flashChangesPerSecond = 15.5f;
    };
    NameplateOptions GetNameplateOptions();
    void SetNameplateOptions(const NameplateOptions& options);
    void TickNameplateOptions();
    void SetNativePetOwnerNames(bool enabled);
    std::uint32_t GetNameplateTagColor(void* spawn);

    bool InstallNameplateHooks(
        HMODULE module,
        const ValidatedClient& validation);
}
