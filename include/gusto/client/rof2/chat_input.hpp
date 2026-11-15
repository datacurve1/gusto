#pragma once

#include "gusto/client/rof2/compatibility.hpp"

#include <windows.h>

namespace gusto::client::rof2 {

bool InitializeChatInput(HMODULE module, const ValidatedClient& validation);

} // namespace gusto::client::rof2
