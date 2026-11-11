#pragma once

#include <windows.h>
#include "gusto/client/rof2/compatibility.hpp"

namespace gusto::client::rof2
{
    bool InstallMercenaryUiExpansion(
        HMODULE module,
        const ValidatedClient& validation);
}
