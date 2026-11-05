#pragma once

#include <windows.h>
#include <cstddef>

#include "gusto/client/rof2/addresses.hpp"
#include "gusto/client/rof2/compatibility.hpp"

namespace gusto::client::rof2
{
    void* ResolveObject(
        HMODULE module,
        const ValidatedClient& validation,
        const addresses::Global& global,
        std::size_t objectSize);

    void* ResolvePointer(
        HMODULE module,
        const ValidatedClient& validation,
        const addresses::Global& global,
        std::size_t targetSize);
}
