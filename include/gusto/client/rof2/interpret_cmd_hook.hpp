#pragma once

#include "gusto/client/rof2/compatibility.hpp"

namespace gusto::client::rof2
{
    bool InstallInterpretCmdHook(
        HMODULE module,
        const ValidatedClient& validation);
}
