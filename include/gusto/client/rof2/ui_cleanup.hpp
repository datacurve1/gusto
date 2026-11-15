#pragma once
#include "gusto/client/rof2/compatibility.hpp"
namespace gusto::client::rof2 {
bool InitializeUiCleanup(HMODULE module, const ValidatedClient& validation);
void TickUiCleanup();
}
