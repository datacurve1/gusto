#include "gusto/client/rof2/gamepad.hpp"
#include <atomic>
#include <windows.h>
#include <xinput.h>
#include <mmsystem.h>
#include <cstring>
#include <cwchar>
#include <cstdio>
#include <cctype>
namespace gusto::client::rof2 {
namespace {
pad::Options options;
pad::Edges edges,mouseEdges;pad::KeyboardEdges keyboard;
bool injectedKeys[256]{};bool initializedBackend=false;
struct NativeKeyCombo {BYTE alt,control,shift,scan;};
static_assert(sizeof(NativeKeyCombo)==4);
using NativeKeyFn=bool(__thiscall*)(void*,const NativeKeyCombo*);
using NativeScanUp=bool(__cdecl*)(unsigned);
NativeKeyFn nativeKeyDown=nullptr,nativeKeyUp=nullptr;NativeScanUp physicalKeyUp=nullptr;
std::uintptr_t keyManagerStorage=0;
std::array<bool,pad::Actions> nativeHeld{};std::array<NativeKeyCombo,pad::Actions> nativeCombos{};std::uintptr_t nativeOwner=0;
unsigned nativeDownCount=0,nativeUpCount=0,lastBlockedReasons=0;bool lastNativeHandled=false,lastKeyAllowed=false;int lastNativeBinding=-1;
pad::KeyCapture keyboardCapture;ULONGLONG captureQuietUntil=0;
SRWLOCK captureLock=SRWLOCK_INIT;
struct CaptureGuard{CaptureGuard(){AcquireSRWLockExclusive(&captureLock);}~CaptureGuard(){ReleaseSRWLockExclusive(&captureLock);}};
void* NativeManager(){void* manager=nullptr;SIZE_T got=0;if(!keyManagerStorage||!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(keyManagerStorage),&manager,sizeof(manager),&got)||got!=sizeof(manager))return nullptr;return manager;}
bool NativeBinding(int binding,bool down){
 if(!pad::KeyBinding(binding)||!nativeKeyDown||!nativeKeyUp)return false;
 void* manager=NativeManager();if(!manager)return false;
 UINT scan=pad::DirectInputScan(binding&255,MapVirtualKeyW(binding&255,MAPVK_VK_TO_VSC_EX));if(!scan)return false;
 NativeKeyCombo key{BYTE((binding&pad::Alt)!=0),BYTE((binding&pad::Control)!=0),BYTE((binding&pad::Shift)!=0),BYTE(scan)};
 if(down)nativeCombos[binding]=key;else if(nativeHeld[binding])key=nativeCombos[binding];
 bool handled=(down?nativeKeyDown:nativeKeyUp)(manager,&key);
 if(down){++nativeDownCount;lastNativeBinding=binding;lastNativeHandled=handled;}else ++nativeUpCount;
 return true;
}
pad::MouseMotion mouseMotion;pad::StickDirections stickDirections;
ULONGLONG lastPoll=0;
bool injectedButtons[3]{};
using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
GetState getState=nullptr;
using GetJoy=MMRESULT(WINAPI*)(UINT,LPJOYINFOEX);
using GetJoyCaps=MMRESULT(WINAPI*)(UINT_PTR,LPJOYCAPSW,UINT);
GetJoy getJoy=nullptr;GetJoyCaps getJoyCaps=nullptr;
JOYCAPSW joyCaps{};int capsSlot=-1,lastCapsSlot=-1,activeBackend=0;ULONGLONG capsTick=0;
const WORD canonicalButtons[]={XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_X,XINPUT_GAMEPAD_Y,XINPUT_GAMEPAD_LEFT_SHOULDER,XINPUT_GAMEPAD_RIGHT_SHOULDER,XINPUT_GAMEPAD_BACK,XINPUT_GAMEPAD_START,XINPUT_GAMEPAD_LEFT_THUMB,XINPUT_GAMEPAD_RIGHT_THUMB,XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_DOWN,XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_RIGHT};
bool ReadJoystick(XINPUT_STATE& state){
 if(!getJoy || !getJoyCaps)return false;
 auto now=GetTickCount64();
 if(capsSlot!=options.slot && lastCapsSlot==options.slot && now-capsTick<1000)return false;
 if(capsSlot!=options.slot || now-capsTick>=1000){
  lastCapsSlot=options.slot;
  capsSlot=-1;capsTick=now;
  if(getJoyCaps(options.slot,&joyCaps,sizeof(joyCaps))!=JOYERR_NOERROR)return false;
  capsSlot=options.slot;
 }
 if(capsSlot!=options.slot)return false;
 JOYINFOEX position{};position.dwSize=sizeof(position);position.dwFlags=JOY_RETURNALL;
 if(getJoy(options.slot,&position)!=JOYERR_NOERROR){capsSlot=-1;return false;}
 pad::JoystickSample raw;
 const DWORD values[]={position.dwXpos,position.dwYpos,position.dwZpos,position.dwRpos,position.dwUpos,position.dwVpos};
 const UINT lo[]={joyCaps.wXmin,joyCaps.wYmin,joyCaps.wZmin,joyCaps.wRmin,joyCaps.wUmin,joyCaps.wVmin};
 const UINT hi[]={joyCaps.wXmax,joyCaps.wYmax,joyCaps.wZmax,joyCaps.wRmax,joyCaps.wUmax,joyCaps.wVmax};
 for(int i=0;i<6;++i)raw.axes[i]=pad::NormalizeJoystickAxis(values[i],lo[i],hi[i]);
 if(options.joystickLayout==2 && !(joyCaps.wCaps&JOYCAPS_HASU))raw.axes[4]=-1;
 if(options.joystickLayout==2 && !(joyCaps.wCaps&JOYCAPS_HASV))raw.axes[5]=-1;
 raw.buttons=position.dwButtons;raw.pov=(joyCaps.wCaps&JOYCAPS_HASPOV)&&position.dwPOV!=JOY_POVCENTERED?int(position.dwPOV):-1;
 auto mapped=pad::MapJoystick(raw,options);
 for(int i=0;i<14;++i)if(mapped.buttons[i])state.Gamepad.wButtons|=canonicalButtons[i];
 state.Gamepad.bLeftTrigger=mapped.buttons[14]?255:0;state.Gamepad.bRightTrigger=mapped.buttons[15]?255:0;
 state.Gamepad.sThumbLX=SHORT(mapped.sticks[0]*32767);state.Gamepad.sThumbLY=SHORT(mapped.sticks[1]*32767);
 state.Gamepad.sThumbRX=SHORT(mapped.sticks[2]*32767);state.Gamepad.sThumbRY=SHORT(mapped.sticks[3]*32767);
 return true;
}
std::atomic_int capturedInput{-1};std::atomic_bool captureInput{false};pad::Inputs previousInputs{};
bool connected=false;
const char* inputs[]={"A / Cross","B / Circle","X / Square","Y / Triangle","Left bumper","Right bumper","Back / Share","Start / Options","Left stick click","Right stick click","D-pad up","D-pad down","D-pad left","D-pad right","Left trigger","Right trigger","Left stick left","Left stick right","Left stick up","Left stick down","Right stick left","Right stick right","Right stick up","Right stick down"};
void MouseButton(int id,bool down){
 int button=id-pad::MouseLeft;if(button<0 || button>=3)return;
 if(!down && !injectedButtons[button])return;
 INPUT event{};event.type=INPUT_MOUSE;
 const DWORD flags[3][2]={{MOUSEEVENTF_LEFTUP,MOUSEEVENTF_LEFTDOWN},{MOUSEEVENTF_RIGHTUP,MOUSEEVENTF_RIGHTDOWN},{MOUSEEVENTF_MIDDLEUP,MOUSEEVENTF_MIDDLEDOWN}};
 event.mi.dwFlags=flags[button][down?1:0];
 if(SendInput(1,&event,sizeof(event))==1)injectedButtons[button]=down;
}
void KeyButton(int key,bool down){
 if(key<=6 || key>=255 || (!down && !injectedKeys[key]))return;
 if(down && !injectedKeys[key] && (GetAsyncKeyState(key)&0x8000))return;
 INPUT event{};event.type=INPUT_KEYBOARD;UINT scan=MapVirtualKeyW(key,MAPVK_VK_TO_VSC_EX);
 if(scan && key!=VK_PAUSE){event.ki.wScan=WORD(scan&255);event.ki.dwFlags=KEYEVENTF_SCANCODE;if(scan&0xff00)event.ki.dwFlags|=KEYEVENTF_EXTENDEDKEY;}
 else event.ki.wVk=WORD(key);
 if(!down)event.ki.dwFlags|=KEYEVENTF_KEYUP;
 if(SendInput(1,&event,sizeof(event))==1)injectedKeys[key]=down;
}

}
bool InitializeGamepad(HMODULE game,const ValidatedClient& token){
 if(!token.IsValidFor(game))return false;
 const auto down=ResolvePreferredVA(game,0x5594e0,5),up=ResolvePreferredVA(game,0x559800,7),scanUp=ResolvePreferredVA(game,0x5f9ad0,5);
 const BYTE expectedDown[]={0x51,0x53,0x55,0x8b,0xe9},expectedUp[]={0x51,0x53,0x55,0x56,0x57,0x8b,0xf9},expectedScanUp[]={0x53,0x8b,0x5c,0x24,0x08};
 auto matches=[](std::uintptr_t address,const BYTE* bytes,std::size_t size){BYTE data[8]{};SIZE_T got=0;if(!address||!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),data,size,&got)||got!=size)return false;if(!std::memcmp(data,bytes,size))return true;
  if(size>=5 && data[0]==0xe9){std::int32_t displacement=0;std::memcpy(&displacement,data+1,4);auto destination=address+5+displacement;MEMORY_BASIC_INFORMATION info{};return VirtualQuery(reinterpret_cast<void*>(destination),&info,sizeof(info)) && info.State==MEM_COMMIT && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) && (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));}return false;};
 if(matches(down,expectedDown,sizeof(expectedDown))&&matches(up,expectedUp,sizeof(expectedUp))&&matches(scanUp,expectedScanUp,sizeof(expectedScanUp))){
  nativeKeyDown=reinterpret_cast<NativeKeyFn>(down);nativeKeyUp=reinterpret_cast<NativeKeyFn>(up);physicalKeyUp=reinterpret_cast<NativeScanUp>(scanUp);keyManagerStorage=ResolvePreferredVA(game,0xe639b0,4);
 }
 initializedBackend=true;
 for(auto dll:{L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"}){
  HMODULE lib=LoadLibraryW(dll);if(!lib)continue;
  getState=reinterpret_cast<GetState>(GetProcAddress(lib,"XInputGetState"));
  if(getState)break;FreeLibrary(lib);
 }
 if(auto lib=LoadLibraryW(L"winmm.dll")){
  getJoy=reinterpret_cast<GetJoy>(GetProcAddress(lib,"joyGetPosEx"));getJoyCaps=reinterpret_cast<GetJoyCaps>(GetProcAddress(lib,"joyGetDevCapsW"));
  if(!getJoy || !getJoyCaps){getJoy=nullptr;getJoyCaps=nullptr;FreeLibrary(lib);}
 }
 return getState || getJoy;
}
void LoadGamepad(const wchar_t* path){
 options.backend=GetPrivateProfileIntW(L"Gamepad",L"Backend",0,path);
 options.joystickLayout=GetPrivateProfileIntW(L"Gamepad",L"JoystickLayout",0,path);
 for(int i=0;i<16;++i)options.joystickButtons[i]=int(GetPrivateProfileIntW(L"Gamepad",(L"JoystickButton"+std::to_wstring(i)).c_str(),options.joystickButtons[i],path));
 options.enabled=GetPrivateProfileIntW(L"Gamepad",L"Enabled",0,path)!=0;
 options.mouseStick=GetPrivateProfileIntW(L"Gamepad",L"MouseStick",0,path);
 options.mouseSpeed=float(GetPrivateProfileIntW(L"Gamepad",L"MouseSpeed",900,path));
 options.invertMouseY=GetPrivateProfileIntW(L"Gamepad",L"InvertMouseY",0,path)!=0;
 options.slot=GetPrivateProfileIntW(L"Gamepad",L"Slot",0,path);
 options.deadzone=float(GetPrivateProfileIntW(L"Gamepad",L"DeadzonePercent",25,path))/100;
 options.bindings=pad::Options{}.bindings;
 const bool keyFormat=GetPrivateProfileIntW(L"Gamepad",L"BindingFormat",0,path)==2;
 for(int i=0;i<pad::Count;++i){
  auto key=(keyFormat?L"KeyBinding":L"Binding")+std::to_wstring(i);
  int value=int(GetPrivateProfileIntW(L"Gamepad",key.c_str(),keyFormat?options.bindings[i]:-1,path));
  if(keyFormat)options.bindings[i]=value;
  else if(value>=479 && value<=481)options.bindings[i]=pad::MouseLeft+(value-479);
 }
 pad::Clamp(options);
}
void SaveGamepad(const wchar_t* path){
 auto put=[&](const wchar_t* key,int value){auto text=std::to_wstring(value);WritePrivateProfileStringW(L"Gamepad",key,text.c_str(),path);};
 put(L"Backend",options.backend);put(L"JoystickLayout",options.joystickLayout);
 for(int i=0;i<16;++i)put((L"JoystickButton"+std::to_wstring(i)).c_str(),options.joystickButtons[i]);
 put(L"MouseStick",options.mouseStick);put(L"MouseSpeed",int(options.mouseSpeed));put(L"InvertMouseY",options.invertMouseY);
 put(L"Enabled",options.enabled);put(L"Slot",options.slot);put(L"DeadzonePercent",int(options.deadzone*100+.5f));
 put(L"BindingFormat",2);
 for(int i=0;i<pad::Count;++i)put((L"KeyBinding"+std::to_wstring(i)).c_str(),options.bindings[i]);
}
pad::Options GetGamepadOptions(){return options;}
void SetGamepadOptions(pad::Options value){pad::Clamp(value);options=value;}
void ReleaseGamepad(bool ready){
 for(int binding=0;binding<pad::MouseLeft;++binding)if(nativeHeld[binding]){if(reinterpret_cast<std::uintptr_t>(NativeManager())==nativeOwner)NativeBinding(binding,false);nativeHeld[binding]=false;}
 edges.Step({},options,false,[](int,bool){});
 keyboard.Step(edges.held,[](int key,bool down){KeyButton(key,down);});
 for(int i=0;i<256;++i)if(injectedKeys[i])KeyButton(i,false);
 mouseEdges.Step({},options,false,[&](int id,bool down){MouseButton(id,down);});
 for(int i=0;i<3;++i)if(injectedButtons[i])MouseButton(pad::MouseLeft+i,false);
 mouseMotion.Reset();stickDirections.Reset();lastPoll=0;
}
void PollGamepad(bool allowed,bool ready,HWND window,bool mouseAllowed,unsigned gateReasons){
 XINPUT_STATE state{};int source=0;
 if(options.backend!=2 && options.slot<4 && getState && getState(options.slot,&state)==ERROR_SUCCESS)source=1;
 if(!source && options.backend!=1){state={};if(ReadJoystick(state))source=2;}
 if(source!=activeBackend){ReleaseGamepad(ready);activeBackend=source;}
 connected=source!=0;
 pad::Inputs input{};
 if(connected){
  const WORD buttons[]={XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_X,XINPUT_GAMEPAD_Y,XINPUT_GAMEPAD_LEFT_SHOULDER,XINPUT_GAMEPAD_RIGHT_SHOULDER,XINPUT_GAMEPAD_BACK,XINPUT_GAMEPAD_START,XINPUT_GAMEPAD_LEFT_THUMB,XINPUT_GAMEPAD_RIGHT_THUMB,XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_DOWN,XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_RIGHT};
  for(int i=0;i<14;++i)input[i]=(state.Gamepad.wButtons&buttons[i])!=0;
  input[14]=state.Gamepad.bLeftTrigger>30;input[15]=state.Gamepad.bRightTrigger>30;
  const std::array<int,4> axes={state.Gamepad.sThumbLX,state.Gamepad.sThumbLY,state.Gamepad.sThumbRX,state.Gamepad.sThumbRY};
  stickDirections.Step(axes,options.deadzone,input);
 }
 const bool captureBlocked=captureInput.load()||GamepadKeyboardCaptureBlocking();
 if(captureInput.load())for(int i=0;i<pad::Count;++i)if(input[i] && !previousInputs[i]){capturedInput=i;captureInput=false;break;}
 previousInputs=input;
 auto gameOptions=options,mouseOptions=options;
 for(int i=0;i<pad::Count;++i){
  if(gameOptions.bindings[i]>=pad::MouseLeft)gameOptions.bindings[i]=-1;
  if(mouseOptions.bindings[i]<pad::MouseLeft)mouseOptions.bindings[i]=-1;
 }
 if(options.mouseStick)for(int i=16+(options.mouseStick-1)*4;i<20+(options.mouseStick-1)*4;++i){gameOptions.bindings[i]=-1;mouseOptions.bindings[i]=-1;}
 const bool keyAllowed=allowed && ready && connected && window && GetForegroundWindow()==window && !captureBlocked;
 lastKeyAllowed=keyAllowed;
 lastBlockedReasons=gateReasons|(!ready?1u:0u)|(!window?2u:0u)|(window && GetForegroundWindow()!=window?4u:0u)|(captureBlocked?64u:0u)|(!connected?128u:0u)|(!options.enabled?256u:0u);
 edges.Step(input,gameOptions,keyAllowed,[](int,bool){});
 if(nativeKeyDown && nativeKeyUp){
  auto owner=reinterpret_cast<std::uintptr_t>(NativeManager());if(nativeOwner!=owner){nativeHeld.fill(false);nativeOwner=owner;}
  for(int binding=0;binding<pad::MouseLeft;++binding)if(nativeHeld[binding]&&!edges.held[binding]){NativeBinding(binding,false);nativeHeld[binding]=false;}
  for(int binding=0;binding<pad::MouseLeft;++binding)if(!nativeHeld[binding]&&edges.held[binding])nativeHeld[binding]=NativeBinding(binding,true);
 }else{
  keyboard.Step(edges.held,[](int key,bool down){KeyButton(key,down);});
  for(int key=0;key<256;++key){if(keyboard.held[key]&&!injectedKeys[key])KeyButton(key,true);else if(!keyboard.held[key]&&injectedKeys[key])KeyButton(key,false);}
 }
 RECT client{};POINT cursor{};
 bool inside=window && GetForegroundWindow()==window && GetClientRect(window,&client) && GetCursorPos(&cursor) && ScreenToClient(window,&cursor) && PtInRect(&client,cursor);
 bool canMouse=mouseAllowed && ready && connected && inside && !captureBlocked;
 mouseEdges.Step(input,mouseOptions,canMouse,[&](int id,bool down){MouseButton(id,down);});
 ULONGLONG now=GetTickCount64();float dt=lastPoll?float(now-lastPoll)/1000:0;lastPoll=now;
 if(canMouse && options.enabled && options.mouseStick && mouseEdges.armed){
  float x=float(options.mouseStick==1?state.Gamepad.sThumbLX:state.Gamepad.sThumbRX)/32768;
  float y=float(options.mouseStick==1?state.Gamepad.sThumbLY:state.Gamepad.sThumbRY)/32768;
  auto motion=mouseMotion.Step(x,y,options.deadzone,options.mouseSpeed,dt,options.invertMouseY);
  // Keep pointer input confined to this game's client area.
  motion[0]=std::clamp(cursor.x+motion[0],client.left,client.right-1)-cursor.x;
  motion[1]=std::clamp(cursor.y+motion[1],client.top,client.bottom-1)-cursor.y;
  if(motion[0] || motion[1]){
   POINT target{cursor.x+motion[0],cursor.y+motion[1]};ClientToScreen(window,&target);
   INPUT event{};event.type=INPUT_MOUSE;int width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
   if(width>1 && height>1){event.mi.dx=LONG((target.x-GetSystemMetrics(SM_XVIRTUALSCREEN))*65535LL/(width-1));event.mi.dy=LONG((target.y-GetSystemMetrics(SM_YVIRTUALSCREEN))*65535LL/(height-1));event.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;SendInput(1,&event,sizeof(event));}
  }
 } else mouseMotion.Reset();
 if(!canMouse || !options.enabled)for(int i=0;i<3;++i)if(injectedButtons[i])MouseButton(pad::MouseLeft+i,false);
}
void BeginGamepadInputCapture(){capturedInput=-1;captureInput=true;}
void CancelGamepadInputCapture(){captureInput=false;capturedInput=-1;}
int ConsumeGamepadInputCapture(){return capturedInput.exchange(-1);}
void BeginGamepadKeyboardCapture(){
 ReleaseGamepad(true);CaptureGuard guard;int mods=0;
 if(GetAsyncKeyState(VK_SHIFT)&0x8000)mods|=pad::Shift;if(GetAsyncKeyState(VK_CONTROL)&0x8000)mods|=pad::Control;if(GetAsyncKeyState(VK_MENU)&0x8000)mods|=pad::Alt;
 keyboardCapture.Begin(mods);
}
void CancelGamepadKeyboardCapture(){CaptureGuard guard;if(keyboardCapture.active||keyboardCapture.draining)keyboardCapture.Cancel();}
void FeedGamepadKeyboardCapture(int vk,bool down){CaptureGuard guard;keyboardCapture.Event(vk,down);}
bool GamepadKeyboardCaptureBlocking(){CaptureGuard guard;return keyboardCapture.active||keyboardCapture.draining||GetTickCount64()<captureQuietUntil;}
void DrainGamepadKeyboardCapture(bool neutral){CaptureGuard guard;bool wasDraining=keyboardCapture.draining;keyboardCapture.Drain(neutral);if(wasDraining&&!keyboardCapture.draining)captureQuietUntil=GetTickCount64()+80;}
int ConsumeGamepadKeyboardCapture(){CaptureGuard guard;return keyboardCapture.Consume();}
void ReleaseNativePhysicalKey(unsigned scan){if(physicalKeyUp)physicalKeyUp(scan);}
std::string GamepadKeyboardStatus(){return std::string("EQ key bridge=")+(nativeKeyDown&&nativeKeyUp?"ready":"unavailable")+", allowed="+(lastKeyAllowed?"yes":"no (menu/chat/focus)")+", gate="+std::to_string(lastBlockedReasons)+", down="+std::to_string(nativeDownCount)+", up="+std::to_string(nativeUpCount)+", last="+GamepadCommandName(lastNativeBinding)+", matched="+(lastNativeHandled?"yes":"no");}
const char* GamepadInputName(int i){return i>=0&&i<pad::Count?inputs[i]:"Unknown";}
std::string GamepadCommandName(int id){
 if(id==pad::MouseLeft)return "MOUSE_LEFT";if(id==pad::MouseRight)return "MOUSE_RIGHT";if(id==pad::MouseMiddle)return "MOUSE_MIDDLE";
 if(!pad::KeyBinding(id))return "Unbound";
 std::string result;if(id&pad::Control)result+="Ctrl+";if(id&pad::Alt)result+="Alt+";if(id&pad::Shift)result+="Shift+";
 int key=id&255;
 if((key>=0x30 && key<=0x39)||(key>=0x41 && key<=0x5a))result+=char(key);
 else if(key>=VK_F1 && key<=VK_F24)result+="F"+std::to_string(key-VK_F1+1);
 else if(key>=VK_NUMPAD0 && key<=VK_NUMPAD9)result+="Numpad "+std::to_string(key-VK_NUMPAD0);
 else {
  const std::pair<int,const char*> keys[]={{VK_SPACE,"Space"},{VK_RETURN,"Enter"},{VK_TAB,"Tab"},{VK_ESCAPE,"Escape"},{VK_BACK,"Backspace"},{VK_UP,"Up"},{VK_DOWN,"Down"},{VK_LEFT,"Left"},{VK_RIGHT,"Right"},{VK_HOME,"Home"},{VK_END,"End"},{VK_PRIOR,"Page Up"},{VK_NEXT,"Page Down"},{VK_INSERT,"Insert"},{VK_DELETE,"Delete"},{VK_SHIFT,"Shift"},{VK_CONTROL,"Ctrl"},{VK_MENU,"Alt"},{VK_ADD,"Numpad +"},{VK_SUBTRACT,"Numpad -"},{VK_MULTIPLY,"Numpad *"},{VK_DIVIDE,"Numpad /"},{VK_DECIMAL,"Numpad ."},{VK_OEM_MINUS,"-"},{VK_OEM_PLUS,"="},{VK_OEM_COMMA,","},{VK_OEM_PERIOD,"."},{VK_OEM_1,";"},{VK_OEM_2,"/"},{VK_OEM_3,"`"},{VK_OEM_4,"["},{VK_OEM_5,"\\"},{VK_OEM_6,"]"},{VK_OEM_7,"'"}};
  bool found=false;for(auto [vk,name]:keys)if(key==vk){result+=name;found=true;break;}if(!found)result+="VK "+std::to_string(key);
 }
 return result;
}
int ParseGamepadBinding(std::string_view text){
 auto normalize=[](std::string_view s){std::string result;for(unsigned char ch:s)if(ch!='_'&&!std::isspace(ch))result+=char(std::toupper(ch));return result;};
 auto name=normalize(text);if(name=="UNBOUND"||name=="NONE")return -1;
 for(int id=0;id<pad::Actions;++id)if(pad::ValidBinding(id)&&normalize(GamepadCommandName(id))==name)return id;
 return -2;
}
int CycleGamepadCommand(int id,int direction){int next=id+direction;return next< -1?pad::MouseMiddle:next>pad::MouseMiddle?-1:next;}
std::string GamepadStatus(){return !initializedBackend?"Controller input not initialized":connected?(activeBackend==2?"Connected: joystick":"Connected: XInput"):(!getState && !getJoy)?"Controller backends unavailable":"No controller in selected slot";}
}
