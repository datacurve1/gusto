#pragma once
#include "gusto/client/rof2/compatibility.hpp"
namespace gusto::client::rof2 {
bool InstallServerSelectHook(HMODULE client, const ValidatedClient& validation, HMODULE frontend);
void StartServerSelectHooks(HMODULE client, const ValidatedClient& validation);
void StopServerSelectHooks();
}
