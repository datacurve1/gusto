// Exercise the real polling, foreground gate, SendInput and release paths.
#include "../src/client/rof2/gamepad.cpp"
#include <cstdlib>
#include <vector>
namespace gusto::client::rof2 {
// This harness never authorizes or reads an EQ executable.
std::uintptr_t ResolvePreferredVA(HMODULE,std::uintptr_t,std::size_t){return 0;}
}
using namespace gusto::client::rof2;
namespace {
XINPUT_STATE fake{};bool plugged=true;std::vector<std::pair<int,bool>> keyEvents;
unsigned downs[3]{},ups[3]{};
std::vector<std::pair<NativeKeyCombo,bool>> nativeEvents;
int fakeManagerMode=0;bool fakeFocusedWindow=false;
bool __thiscall FakeKeyDown(void*,const NativeKeyCombo* key){nativeEvents.emplace_back(*key,true);fakeManagerMode=20;return true;}
bool __thiscall FakeKeyUp(void*,const NativeKeyCombo* key){nativeEvents.emplace_back(*key,false);fakeManagerMode=0;return true;}
DWORD WINAPI FakePad(DWORD,XINPUT_STATE* out){*out=fake;return plugged?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED;}
JOYINFOEX generic{};bool genericConnected=true;
MMRESULT WINAPI FakeJoystick(UINT,LPJOYINFOEX out){*out=generic;return genericConnected?JOYERR_NOERROR:JOYERR_UNPLUGGED;}
MMRESULT WINAPI FakeCaps(UINT_PTR,LPJOYCAPSW caps,UINT){
 *caps={};caps->wXmax=caps->wYmax=caps->wZmax=caps->wRmax=caps->wUmax=caps->wVmax=65535;
 caps->wCaps=JOYCAPS_HASPOV|JOYCAPS_HASZ|JOYCAPS_HASR|JOYCAPS_HASU|JOYCAPS_HASV;return JOYERR_NOERROR;
}
LRESULT CALLBACK Window(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 if(msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN)keyEvents.emplace_back(int(wp),true);
 if(msg==WM_KEYUP||msg==WM_SYSKEYUP)keyEvents.emplace_back(int(wp),false);
 const UINT down[3]={WM_LBUTTONDOWN,WM_RBUTTONDOWN,WM_MBUTTONDOWN},up[3]={WM_LBUTTONUP,WM_RBUTTONUP,WM_MBUTTONUP};
 for(int i=0;i<3;++i){if(msg==down[i])++downs[i];if(msg==up[i])++ups[i];}
 return DefWindowProcW(w,msg,wp,lp);
}
void Pump(){for(int i=0;i<5;++i){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(5);}}
void Check(bool good,const char* why){if(!good){std::printf("FAIL mouse preview: %s\n",why);std::exit(1);}}
}
int main(){
 auto wineJoystick=LoadLibraryW(L"winmm.dll");Check(wineJoystick && GetProcAddress(wineJoystick,"joyGetPosEx") && GetProcAddress(wineJoystick,"joyGetDevCapsW"),"Wine joystick API exports are available");FreeLibrary(wineJoystick);
 WNDCLASSW wc{};wc.lpfnWndProc=Window;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"GustoGamepadPreview";RegisterClassW(&wc);
 auto window=CreateWindowW(wc.lpszClassName,L"Gusto controller mouse test",WS_OVERLAPPEDWINDOW,30,30,700,500,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(window,SW_SHOW);SetForegroundWindow(window);Pump();
 POINT center{250,180};ClientToScreen(window,&center);SetCursorPos(center.x,center.y);Pump();
 Check(GetForegroundWindow()==window,"test window is foreground");getState=FakePad;initializedBackend=true;
 pad::Options cfg;cfg.enabled=true;cfg.mouseStick=2;cfg.bindings[0]=pad::MouseLeft;cfg.bindings[2]=pad::MouseRight;cfg.bindings[3]=pad::MouseMiddle;SetGamepadOptions(cfg);
 PollGamepad(true,true,window,true);fake.Gamepad.sThumbRX=32767;lastPoll=GetTickCount64()-20;PollGamepad(true,true,window,true);Pump();
 POINT moved{};GetCursorPos(&moved);Check(moved.x>center.x && moved.x<center.x+40,"right stick moves pointer at configured speed");Check(keyEvents.empty(),"mouse stick does not send keyboard directions");fake={};PollGamepad(true,true,window,true);
 const WORD buttons[3]={XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_X,XINPUT_GAMEPAD_Y};
 for(int i=0;i<3;++i){fake.Gamepad.wButtons=buttons[i];PollGamepad(false,true,window,true);Pump();Check(downs[i]==1,"mouse press works while game bindings are blocked by menu");PollGamepad(false,true,window,true);Pump();Check(downs[i]==1,"held mouse button does not repeat presses");fake={};PollGamepad(false,true,window,true);Pump();Check(ups[i]==1,"mouse button release delivered");}
 fake.Gamepad.wButtons=XINPUT_GAMEPAD_A;PollGamepad(false,true,window,true);Pump();plugged=false;PollGamepad(false,true,window,true);Pump();Check(ups[0]==2,"disconnect releases held left click");
 plugged=true;PollGamepad(true,true,window,true);Pump();Check(downs[0]==2,"reconnect with held input must neutral-rearm");fake={};PollGamepad(true,true,window,true);
 fake.Gamepad.wButtons=XINPUT_GAMEPAD_X;PollGamepad(false,true,window,true);Pump();ReleaseGamepad(false);Pump();Check(ups[1]==2,"focus-loss release does not need a live native player");
 fake={};PollGamepad(true,true,window,true);fake.Gamepad.sThumbLX=32767;PollGamepad(true,true,window,true);Pump();Check((GetAsyncKeyState(VK_RIGHT)&0x8000)!=0,"other stick presses actual keyboard Right");ReleaseGamepad(false);Pump();Check((GetAsyncKeyState(VK_RIGHT)&0x8000)==0,"keyboard Right releases even outside gameplay");
 cfg.bindings[1]=pad::Control|'1';SetGamepadOptions(cfg);fake={};PollGamepad(true,true,window,true);fake.Gamepad.wButtons=XINPUT_GAMEPAD_B;PollGamepad(true,true,window,true);Pump();Check((GetAsyncKeyState(VK_CONTROL)&0x8000) && (GetAsyncKeyState('1')&0x8000),"actual Ctrl+1 keyboard chord");ReleaseGamepad(false);Pump();Check(!(GetAsyncKeyState(VK_CONTROL)&0x8000) && !(GetAsyncKeyState('1')&0x8000),"shortcut keys and modifier released");
 Check(ParseGamepadBinding("Ctrl+Shift+F4")==int(pad::Control|pad::Shift|VK_F4),"key shortcut parser");
 // Simulate a Linux joystick visible through Wine's WinMM / DirectInput path.
 getJoy=FakeJoystick;getJoyCaps=FakeCaps;plugged=false;cfg.backend=0;cfg.joystickLayout=0;SetGamepadOptions(cfg);
 generic.dwXpos=generic.dwYpos=generic.dwZpos=generic.dwRpos=generic.dwUpos=generic.dwVpos=32767;generic.dwPOV=JOY_POVCENTERED;
 PollGamepad(true,true,window,true);Check(activeBackend==2 && connected,"Auto falls back to Wine joystick when XInput is absent");
 generic.dwButtons=1;unsigned oldDown=downs[0],oldUp=ups[0];PollGamepad(false,true,window,true);Pump();Check(downs[0]==oldDown+1,"Wine joystick button sends left click");
 generic.dwButtons=0;PollGamepad(false,true,window,true);Pump();Check(ups[0]==oldUp+1,"Wine joystick button releases left click");
 POINT beforeJoystick{};GetCursorPos(&beforeJoystick);generic.dwZpos=65535;lastPoll=GetTickCount64()-20;PollGamepad(false,true,window,true);Pump();GetCursorPos(&moved);Check(moved.x>beforeJoystick.x,"Wine joystick right stick moves cursor");
 generic.dwZpos=32767;PollGamepad(true,true,window,true);generic.dwButtons=1;PollGamepad(false,true,window,true);Pump();oldUp=ups[0];genericConnected=false;PollGamepad(false,true,window,true);Pump();Check(ups[0]==oldUp+1,"Wine disconnect releases held mouse button");
 genericConnected=true;generic.dwButtons=0;capsTick=0;lastCapsSlot=-1;PollGamepad(false,true,window,true);
 BeginGamepadInputCapture();generic.dwButtons=2;PollGamepad(false,true,window,true);Check(ConsumeGamepadInputCapture()==1,"press-to-select captures actual controller button");Check(ConsumeGamepadInputCapture()==-1,"captured selection consumed once");CancelGamepadInputCapture();ReleaseGamepad(true);
 generic.dwButtons=0;PollGamepad(false,true,window,true);BeginGamepadInputCapture();oldDown=downs[0];generic.dwButtons=1;PollGamepad(false,true,window,true);Pump();Check(ConsumeGamepadInputCapture()==0 && downs[0]==oldDown,"capturing an input must not replay its mouse click onto the menu");ReleaseGamepad(true);
 // Verify the production EQ KeypressHandler path, including DI scan codes.
 void* fakeManager=reinterpret_cast<void*>(0x12340000);keyManagerStorage=reinterpret_cast<std::uintptr_t>(&fakeManager);nativeKeyDown=FakeKeyDown;nativeKeyUp=FakeKeyUp;
 generic.dwButtons=0;PollGamepad(true,true,window,true);auto nativeOptions=GetGamepadOptions();nativeOptions.bindings[0]=nativeOptions.bindings[1]=pad::Control|'1';SetGamepadOptions(nativeOptions);
 auto osEvents=keyEvents.size();generic.dwButtons=3;PollGamepad(true,true,window,true);Pump();Check(nativeEvents.size()==1 && nativeEvents[0].second,"native keybinding handler called once for shared shortcut");
 auto combo=nativeEvents[0].first;Check(combo.alt==0 && combo.control==1 && combo.shift==0 && combo.scan==0x02,"RoF2 native KeyCombo ABI and DIK_1 scan code");Check(keyEvents.size()==osEvents,"native route does not also inject duplicate OS keys");
 generic.dwButtons=2;PollGamepad(true,true,window,true);Check(nativeEvents.size()==1,"shared native key stays held");ReleaseGamepad(false);Check(nativeEvents.size()==2 && !nativeEvents.back().second && nativeEvents.back().first.scan==combo.scan,"native shortcut released through the same keybinding handler");
 nativeOptions.bindings[0]=pad::Alt|pad::Shift|VK_RIGHT;SetGamepadOptions(nativeOptions);generic.dwButtons=0;PollGamepad(true,true,window,true);generic.dwButtons=1;PollGamepad(true,true,window,true);
 combo=nativeEvents.back().first;Check(combo.alt==1 && combo.shift==1 && combo.control==0 && combo.scan==0xcd,"extended Right scan and Alt/Shift modifiers packed correctly");ReleaseGamepad(false);
 nativeOptions.bindings[0]=VK_SPACE;SetGamepadOptions(nativeOptions);generic.dwButtons=0;PollGamepad(!pad::KeyboardTypingGate(1,false),true,window,true);
 auto countBeforeSpace=nativeEvents.size();generic.dwButtons=1;PollGamepad(!pad::KeyboardTypingGate(1,false),true,window,true);
 Check(nativeEvents.size()==countBeforeSpace+1 && nativeEvents.back().second && nativeEvents.back().first.scan==0x39,"observed RoF2 normal-play flags deliver Space to the native Jump binding");
 PollGamepad(!pad::KeyboardTypingGate(0,false),true,window,true);Check(!nativeEvents.back().second,"entering actual typing mode releases controller Space");
 auto afterTyping=nativeEvents.size();PollGamepad(!pad::KeyboardTypingGate(1,false),true,window,true);Check(nativeEvents.size()==afterTyping,"held button cannot replay when leaving chat");ReleaseGamepad(false);
 // Sustained analog movement must be one native down until neutral, even
 // across many polls and small fluctuations around the activation deadzone.
 generic.dwButtons=0;generic.dwXpos=generic.dwYpos=32767;
 nativeOptions.bindings[17]=VK_RIGHT;nativeOptions.bindings[18]=VK_UP;SetGamepadOptions(nativeOptions);
 PollGamepad(true,true,window,true);auto beforeStick=nativeEvents.size();
 generic.dwXpos=48000;generic.dwYpos=18000;PollGamepad(true,true,window,true);
 Check(nativeEvents.size()==beforeStick+2 && nativeHeld[VK_RIGHT] && nativeHeld[VK_UP],"diagonal stick starts two native held keys");
 Check(fakeManagerMode==20 && !fakeFocusedWindow,"ordinary native movement key-down enters mode 20 with no UI focus");
 for(int frame=0;frame<1800;++frame){generic.dwXpos=frame%2?40900:43000;PollGamepad(!pad::KeyboardTypingGate(1,fakeFocusedWindow),true,window,true);}
 Check(nativeEvents.size()==beforeStick+2 && nativeHeld[VK_RIGHT] && nativeHeld[VK_UP],"stick keeps keys held across 1800 polls and deadzone jitter without tapping");
 fakeFocusedWindow=true;PollGamepad(!pad::KeyboardTypingGate(1,fakeFocusedWindow),true,window,true);
 Check(!nativeHeld[VK_RIGHT]&&!nativeHeld[VK_UP],"real UI focus releases movement even when chat mode remains normal");
 fakeFocusedWindow=false;generic.dwXpos=generic.dwYpos=32767;PollGamepad(true,true,window,true);
 generic.dwXpos=48000;generic.dwYpos=18000;PollGamepad(true,true,window,true);
 beforeStick=nativeEvents.size()-2;
 generic.dwXpos=generic.dwYpos=32767;PollGamepad(true,true,window,true);
 Check(nativeEvents.size()==beforeStick+4 && !nativeHeld[VK_RIGHT] && !nativeHeld[VK_UP],"centering stick releases both native movement keys exactly once");
 generic.dwXpos=48000;PollGamepad(true,true,window,true);PollGamepad(false,true,window,true);
 Check(!nativeHeld[VK_RIGHT] && !nativeEvents.back().second,"blocked gameplay releases a held stick key");
 auto afterBlock=nativeEvents.size();PollGamepad(true,true,window,true);Check(nativeEvents.size()==afterBlock,"deflected stick must return to neutral after blocked gameplay");
 generic.dwXpos=32767;PollGamepad(true,true,window,true);generic.dwXpos=48000;PollGamepad(true,true,window,true);genericConnected=false;PollGamepad(true,true,window,true);
 Check(!nativeHeld[VK_RIGHT] && !nativeEvents.back().second,"disconnect releases a held stick key");ReleaseGamepad(false);
 nativeKeyDown=nullptr;nativeKeyUp=nullptr;keyManagerStorage=0;
 DestroyWindow(window);std::printf("PASS: actual controller polling, pointer motion, keyboard movement and left/right/middle SendInput edges; menu, disconnect, rearm and release gates\n");
}
