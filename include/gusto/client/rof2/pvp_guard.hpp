#pragma once
#include "gusto/client/rof2/compatibility.hpp"
namespace gusto::client::rof2 {
bool InstallPvpGuard(HMODULE module, const ValidatedClient& validation);
bool RejectPvpOffCommand(void* everquest, const char* command);
}
