#pragma once
#include "gusto/client/rof2/gamepad_model.hpp"
#include "gusto/client/rof2/compatibility.hpp"
#include <string>
#include <string_view>
namespace gusto::client::rof2 {
bool InitializeGamepad(HMODULE,const ValidatedClient&);
void LoadGamepad(const wchar_t* path);
void SaveGamepad(const wchar_t* path);
pad::Options GetGamepadOptions();
void SetGamepadOptions(pad::Options);
void PollGamepad(bool allowed,bool nativeReady,HWND window=nullptr,bool mouseAllowed=false,unsigned gateReasons=0);
void ReleaseGamepad(bool nativeReady);
void BeginGamepadInputCapture();
void CancelGamepadInputCapture();
int ConsumeGamepadInputCapture();
void BeginGamepadKeyboardCapture();
void CancelGamepadKeyboardCapture();
void FeedGamepadKeyboardCapture(int vk,bool down);
bool GamepadKeyboardCaptureBlocking();
void DrainGamepadKeyboardCapture(bool neutral);
int ConsumeGamepadKeyboardCapture();
void ReleaseNativePhysicalKey(unsigned scan);
std::string GamepadKeyboardStatus();
const char* GamepadInputName(int);
std::string GamepadCommandName(int);
int ParseGamepadBinding(std::string_view);
int CycleGamepadCommand(int,int);
std::string GamepadStatus();
}
