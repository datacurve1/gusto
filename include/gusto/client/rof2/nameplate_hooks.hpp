#pragma once

// include/gusto/client/rof2/nameplate_hooks.hpp

#include "gusto/client/rof2/compatibility.hpp"

namespace gusto::client::rof2
{
    bool InstallNameplateHooks(
        HMODULE module,
        const ValidatedClient& validation);
}
