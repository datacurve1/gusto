#pragma once

#include "gusto/client/rof2/hook_utils.hpp"
#include <string>

namespace gusto::client::rof2
{
    inline constexpr unsigned BankSlots = 32;
    inline constexpr unsigned SharedBankSlots = 8;
    // Extra arrays are appended to the existing window; all original fields
    // retain their offsets, including money buttons and BankSize.
    inline constexpr unsigned BankWindowSlotsOffset = 0x2c8;
    inline constexpr unsigned SharedBankWindowSlotsOffset = 0x348;
    inline constexpr unsigned BankWindowSize = 0x368;

    bool InstallBankExpansion(HMODULE module, const ValidatedClient& validation);
    std::string BankExpansionStatus();

    // Exposed for the isolated executable-image test. The generated loop thunk
    // lives for the process lifetime once its patch set has been installed.
    std::vector<CodePatch> BuildBankExpansionPatches(HMODULE module, void* sharedLoop);
}
