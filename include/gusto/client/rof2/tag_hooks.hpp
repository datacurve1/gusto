#pragma once
#include "gusto/client/rof2/compatibility.hpp"
#include <string>
#include <string_view>
namespace gusto::client::rof2 {
bool InstallTagHooks(HMODULE module, const ValidatedClient &validation);
using TagSendCommand = void (*)(void *context, const char *command);
bool HandleTagCommand(std::string_view command, void *context,
                      TagSendCommand send, std::string &reply);
void TickTags();
std::string GetActorTagText(void *actor);
} // namespace gusto::client::rof2
