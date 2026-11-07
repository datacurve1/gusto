#pragma once

// include/gusto/client/rof2/slow_turn_keys.hpp
//
// Adds two native Options -> Keys commands (IDs 484 / 486):
//   Slow Turn Left, Slow Turn Right

#include "gusto/client/rof2/compatibility.hpp"

namespace gusto::client::rof2 {

// Verifies the loaded module token and all expected code bytes before patching.
bool install_slow_turn_keys(HMODULE module, const ValidatedClient& validation);
void shutdown_slow_turn_keys();

}  // namespace gusto::client::rof2
