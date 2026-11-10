#pragma once
#include "gusto/client/rof2/compatibility.hpp"
#include <d3d9.h>
#include <string>
#include <string_view>
namespace gusto::client::rof2 {
bool InstallHudHooks(HMODULE module, const ValidatedClient &validation);
void TickHudHooks();
void ReadHudLocalResources(int& mana,int& maxMana,int& endurance,int& maxEndurance);
void DrawHudWorldBars(IDirect3DDevice9 *device, void *render);
void DrawHudOverlay(IDirect3DDevice9 *device, void *render);
void ResetHudState();
void RemoveHudSpawn(std::uint32_t id,void* object=nullptr);
bool HandleHudCommand(std::string_view text, std::string &reply);
void ShutdownHudOverlay();
} // namespace gusto::client::rof2
