// Offline graphical smoke test: uses the actual overlay renderer with
// synthetic map data, never the game executable or proprietary game assets.
#include "../src/client/rof2/hud_overlay.cpp"
namespace {
struct FakeMouse {
  void** vtable;
  unsigned reads=0,states=0;
  bool saturated=false;
};
HRESULT __stdcall FakeData(void* self,DWORD,DIDEVICEOBJECTDATA*,DWORD* count,DWORD) {
  auto& device=*static_cast<FakeMouse*>(self); ++device.reads;
  if (!device.saturated) *count=0;
  return DI_OK;
}
HRESULT __stdcall FakeState(void* self,DWORD size,void* state) {
  ++static_cast<FakeMouse*>(self)->states; std::memset(state,0,size); return DI_OK;
}
struct FakeKeyboard {void** vtable;std::vector<DIDEVICEOBJECTDATA> events;};
HRESULT __stdcall KeyboardAcquire(void*){return DI_OK;}
HRESULT __stdcall KeyboardData(void* self,DWORD,DIDEVICEOBJECTDATA* out,DWORD* count,DWORD){auto& keyboard=*static_cast<FakeKeyboard*>(self);*count=std::min<DWORD>(*count,keyboard.events.size());std::copy_n(keyboard.events.begin(),*count,out);keyboard.events.erase(keyboard.events.begin(),keyboard.events.begin()+*count);return DI_OK;}
HRESULT __stdcall KeyboardState(void*,DWORD size,void* out){std::memset(out,0,size);return DI_OK;}
unsigned nativeButtonCalls=0;
void __thiscall FakeNativeButton(void*,int,int) { ++nativeButtonCalls; }
unsigned fallbackInputCalls=0;
void __cdecl FakeProcessMouse() { ++fallbackInputCalls; }
}
int main() {
  using namespace gusto::client::rof2;
  const auto instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = instance;
  wc.lpszClassName = L"GustoHUDPreview";
  RegisterClassW(&wc);
  HWND hwnd =
      CreateWindowW(wc.lpszClassName, L"Gusto HUD preview", WS_OVERLAPPEDWINDOW,
                    0, 0, 920, 740, nullptr, nullptr, instance, nullptr);
  if (!hwnd)
    return 1;
  ShowWindow(hwnd, SW_SHOW);
  auto *api = Direct3DCreate9(D3D_SDK_VERSION);
  if (!api)
    return 2;
  D3DPRESENT_PARAMETERS pp{};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8;
  pp.BackBufferWidth = 900;
  pp.BackBufferHeight = 700;
  pp.hDeviceWindow = hwnd;
  pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;
  IDirect3DDevice9 *device = nullptr;
  auto hr =
      api->CreateDevice(0, D3DDEVTYPE_HAL, hwnd,
                        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
  if (FAILED(hr))
    return 3;
  auto check = [](bool v) {
    if (!v)
      std::exit(4);
  };
  // Exercise the real DirectInput drain against an interface with the native
  // IDirectInputDevice8 vtable layout. Draining is bounded even on overflow.
  void* mouseSlots[32]{};
  mouseSlots[9]=reinterpret_cast<void*>(&FakeState);
  mouseSlots[10]=reinterpret_cast<void*>(&FakeData);
  FakeMouse fakeMouse{mouseSlots};
  FlushMouseDevice(reinterpret_cast<IDirectInputDevice8A*>(&fakeMouse));
  check(fakeMouse.reads==1 && fakeMouse.states==1);
  fakeMouse.reads=fakeMouse.states=0; fakeMouse.saturated=true;
  FlushMouseDevice(reinterpret_cast<IDirectInputDevice8A*>(&fakeMouse));
  check(fakeMouse.reads==4 && fakeMouse.states==1);
  originalProcessMouse=&FakeProcessMouse;
  ProcessMouseHook();
  check(fallbackInputCalls==1); // Unsupported executables keep their native input.
  originalProcessMouse=nullptr;
  originalLeftDown=&FakeNativeButton; originalLeft=&FakeNativeButton;
  originalRightDown=&FakeNativeButton; originalRight=&FakeNativeButton;
  LeftDownHook(nullptr,nullptr,10,10); check(worldLeftHeld);
  LeftHook(nullptr,nullptr,10,10); check(!worldLeftHeld && nativeButtonCalls==2);
  RightDownHook(nullptr,nullptr,10,10); check(worldRightHeld);
  RightHook(nullptr,nullptr,10,10); check(!worldRightHeld && nativeButtonCalls==4);
  originalLeftDown=originalLeft=originalRightDown=originalRight=nullptr;

  // Active profile selection follows the native ProfileManager linked lists.
  std::array<BYTE,0x80> fakePc{};
  std::array<int,2> fakeVb{0,0x40};
  std::array<BYTE,20> normalList{},alternateList{};
  std::array<BYTE,0x3394> manaProfile{};
  auto write=[](auto& bytes,std::size_t offset,const auto& value) {
    std::memcpy(bytes.data()+offset,&value,sizeof(value));
  };
  void* pointer=fakeVb.data(); write(fakePc,8,pointer);
  pointer=normalList.data(); write(fakePc,0x4c,pointer);
  int kind=1; write(fakePc,0x50,kind); write(alternateList,0,kind);
  pointer=alternateList.data(); write(normalList,12,pointer);
  pointer=manaProfile.data(); write(alternateList,4,pointer);
  int manaValue=425; write(manaProfile,0x338c,manaValue);
  int readMana=-1;
  check(ProfileMana(fakePc.data(),readMana)&&readMana==425);
  int enduranceValue=735;write(manaProfile,0x3390,enduranceValue);
  check(ProfileResource(fakePc.data(),0x3390,readMana)&&readMana==735);
  kind=2; write(fakePc,0x50,kind);
  pointer=normalList.data(); write(alternateList,12,pointer);
  check(!ProfileMana(fakePc.data(),readMana)); // Cycle cannot hang the client.
  fakeVb[1]=-1;
  check(!ProfileMana(fakePc.data(),readMana));

  // Persisted UI settings must survive a reload without accessing game memory.
  wchar_t oldAppData[32768]{};
  GetEnvironmentVariableW(L"LOCALAPPDATA",oldAppData,32768);
  const auto settingsRoot=std::filesystem::temp_directory_path()/
      (L"gusto-hud-test-"+std::to_wstring(GetCurrentProcessId()));
  std::filesystem::create_directories(settingsRoot);
  SetEnvironmentVariableW(L"LOCALAPPDATA",settingsRoot.c_str());
  Load();
  cfg.players=false; cfg.healing=false; cfg.barRange=375;cfg.arrowScale=2.25f;cfg.life.npc=false;cfg.life.duration=11;cfg.life.rise=.7f;
  cfg.life.npcColor=0xff65ed96;cfg.life.playerColor=0xffb68bff;cfg.life.npcScale=2.2f;
  auto padOptions=GetGamepadOptions();padOptions.enabled=true;padOptions.slot=2;padOptions.backend=2;padOptions.joystickLayout=1;padOptions.joystickButtons[0]=4;padOptions.deadzone=.4f;padOptions.bindings[0]=pad::MouseLeft;padOptions.mouseStick=2;padOptions.mouseSpeed=1200;padOptions.invertMouseY=true;SetGamepadOptions(padOptions);
  SetNameplateOptions({false,false,false,false,12});
  SetTagDisplayOptions({false,false,false,true,true,true,true});
  SetPipeOptions({false,true,250,true});
  cfg.barWidth=88;cfg.barHeight=9;cfg.barWidths={96,72,60,80};cfg.barHeights={10,6,4,7};cfg.npcHpColor=0xff55ddd0;cfg.barRpg=true;cfg.hpColor=0xffff5555;cfg.manaColor=0xffffffff;cfg.staminaColor=0xffb68bff;
  Save();
  cfg=Settings{}; SetNameplateOptions({}); SetTagDisplayOptions({});
  Load();
  check(GetGamepadOptions().enabled && GetGamepadOptions().slot==2 && GetGamepadOptions().deadzone==.4f && GetGamepadOptions().bindings[0]==pad::MouseLeft && GetGamepadOptions().mouseStick==2 && GetGamepadOptions().mouseSpeed==1200 && GetGamepadOptions().invertMouseY);
  check(cfg.barWidths==std::array<float,4>{96,72,60,80} && cfg.barHeights==std::array<float,4>{10,6,4,7} && cfg.npcHpColor==0xff55ddd0 && cfg.barRpg);
  check(cfg.barWidth==88 && cfg.barHeight==9 && cfg.hpColor==0xffff5555 && cfg.manaColor==0xffffffff && cfg.staminaColor==0xffb68bff);
  check(GetGamepadOptions().backend==2 && GetGamepadOptions().joystickLayout==1 && GetGamepadOptions().joystickButtons[0]==4);
  check(cfg.life.npcColor==0xff65ed96 && cfg.life.playerColor==0xffb68bff && cfg.life.npcScale==2.2f);
  check(!GetPipeOptions().enabled && GetPipeOptions().verbose && GetPipeOptions().delay==250 && GetPipeOptions().zealAlias);
  const auto restoredNames=GetNameplateOptions();
  const auto restoredTags=GetTagDisplayOptions();
  check(!cfg.players&&!cfg.healing&&cfg.barRange==375 && cfg.arrowScale==2.25f && !cfg.life.npc && cfg.life.duration==11 && cfg.life.rise==.7f &&
        !restoredNames.conColors&&!restoredNames.pinkTarget&&!restoredNames.brackets&&
        !restoredNames.attackFlash&&restoredNames.flashChangesPerSecond==12 &&
        !restoredTags.enabled&&!restoredTags.models&&!restoredTags.text &&
        restoredTags.defaultArrow&&restoredTags.filter&&restoredTags.suppress&&restoredTags.prettyprint);
  WritePrivateProfileStringW(L"Gamepad",L"BindingFormat",L"1",settingsPath.c_str());
  WritePrivateProfileStringW(L"Gamepad",L"Binding0",L"480",settingsPath.c_str());
  WritePrivateProfileStringW(L"Gamepad",L"Binding1",L"37",settingsPath.c_str());Load();
  check(GetGamepadOptions().bindings[0]==pad::MouseRight && GetGamepadOptions().bindings[1]==pad::Options{}.bindings[1]);
  // Remove only the files just created by this test in its unique directory.
  std::filesystem::remove(settingsPath);
  std::filesystem::remove(settingsPath.parent_path());
  std::filesystem::remove(settingsRoot);
  settingsPath.clear();
  SetEnvironmentVariableW(L"LOCALAPPDATA",oldAppData[0]?oldAppData:nullptr);
  cfg=Settings{}; SetNameplateOptions({}); SetTagDisplayOptions({});
  void* keyVtable[32]{};keyVtable[7]=reinterpret_cast<void*>(&KeyboardAcquire);keyVtable[9]=reinterpret_cast<void*>(&KeyboardState);keyVtable[10]=reinterpret_cast<void*>(&KeyboardData);FakeKeyboard keyDevice{keyVtable,{}};
  for(auto [scan,down]:{std::pair<DWORD,bool>{0x1d,true},{0x02,true},{0x02,false},{0x1d,false}}){DIDEVICEOBJECTDATA event{};event.dwOfs=scan;event.dwData=down?0x80:0;keyDevice.events.push_back(event);}
  BeginGamepadKeyboardCapture();CaptureKeyboardDevice(reinterpret_cast<IDirectInputDevice8A*>(&keyDevice));check(ConsumeGamepadKeyboardCapture()==(pad::Control|'1') && keyDevice.events.empty());
  CancelGamepadKeyboardCapture();DrainGamepadKeyboardCapture(true);
  // The actual DirectInput mouse capture helper accepts each click type;
  // rendering the second wizard stage commits the mouse binding unchanged.
  void* mappingMouseVtable[32]{};mappingMouseVtable[7]=reinterpret_cast<void*>(&KeyboardAcquire);
  mappingMouseVtable[9]=reinterpret_cast<void*>(&KeyboardState);mappingMouseVtable[10]=reinterpret_cast<void*>(&KeyboardData);
  FakeKeyboard mappingMouse{mappingMouseVtable,{}};
  for(int button=0;button<3;++button){
    BeginGamepadKeyboardCapture();
    for(bool down:{true,false}){DIDEVICEOBJECTDATA event{};event.dwOfs=DIMOFS_BUTTON0+button;event.dwData=down?0x80:0;mappingMouse.events.push_back(event);}
    CaptureMappingMouse(reinterpret_cast<IDirectInputDevice8A*>(&mappingMouse));
    check(ConsumeGamepadKeyboardCapture()==pad::MouseLeft+button && mappingMouse.events.empty());
    DrainGamepadKeyboardCapture(true);
  }
  // Drive the actual option actions, including direct mouse and command picking.
  padInput=0;Action(320);check(GetGamepadOptions().bindings[0]==pad::MouseLeft);
  Action(321);check(GetGamepadOptions().bindings[0]==pad::MouseRight);
  Action(322);check(GetGamepadOptions().bindings[0]==pad::MouseMiddle);
  Action(300);Action(343);auto spellChoices=PadCommands();check(!spellChoices.empty() && GamepadCommandName(spellChoices[0])=="F1");
  Action(386);spellChoices=PadCommands();check(GamepadCommandName(spellChoices[0])=="Ctrl+F1");
  Action(4000);check(GetGamepadOptions().bindings[0]==spellChoices[0] && !padPicker);
  Action(323);check(GetGamepadOptions().bindings[0]==-1);Action(320);
  barEdit=1;float hpWidth=cfg.barWidths[0],manaWidth=cfg.barWidths[1];Action(201);check(cfg.barWidths[0]==hpWidth && cfg.barWidths[1]==manaWidth+4);
  barEdit=3;auto playerWidth=cfg.barWidths[0];Action(201);check(cfg.barWidths[3]==52 && cfg.barWidths[0]==playerWidth);Action(214);check(cfg.npcHpColor!=cfg.hpColor);
  barEdit=2;float manaHeight=cfg.barHeights[1];Action(203);check(cfg.barHeights[1]==manaHeight && cfg.barHeights[2]==5);
  // Exercise every menu page and the binding picker, device subpage and RPG art.
  for (int preview=0;preview<14;++preview) {
  int page=preview<9?preview:preview==9?2:7;padListening=preview==12;padKeyListening=preview==13;if(padListening)BeginGamepadInputCapture();if(padKeyListening){CancelGamepadInputCapture();BeginGamepadKeyboardCapture();}padDevicePage=preview==10;padPicker=preview==11;padCategory=3;padPickerPage=0;padModifiers=pad::Control;
  device->Clear(0, nullptr, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, 0xff253544, 1, 0);
  check(SUCCEEDED(device->BeginScene()));
  check(canvas.Init(device));
  canvas.Begin();
  cfg.x = 680;
  cfg.y = 30;
  cfg.scale = 1;
  menu = true;
  menuPage=page;
  buttons.clear();
  cfg.camera = false;
  zoneName = "Plane of Knowledge";
  map = hud::ParseMap(
      "L -500,-500,0,500,-500,0,170,190,200\nL "
      "500,-500,0,500,500,0,170,190,200\nL 500,500,0,-500,500,0,170,190,200\nL "
      "-500,500,0,-500,-500,0,170,190,200\nL -500,0,0,500,0,0,220,210,160\nL "
      "0,-500,0,0,500,0,220,210,160\nL -200,-150,0,200,-150,0,170,190,200\nL "
      "200,-150,0,200,150,0,170,190,200\nL 200,150,0,-200,150,0,170,190,200\nL "
      "-200,150,0,-200,-150,0,170,190,200\n",
      0);
  D3DVIEWPORT9 viewport{0, 0, 900, 700, 0, 1};
  PlayerSnapshot player{};
  player.heading = 64;
  std::array<BYTE,0x1200> fakeSpawn{};
  std::array<BYTE,0x200> fakeActor{};
  std::array<BYTE,0x1800> fakeRender{};
  void* actorPointer=fakeActor.data();
  std::uint32_t fakeId=321;
  localId=fakeId;
  const Point fakeActorPosition{.75f,0,0};
  std::memcpy(fakeActor.data()+0x114,&fakeActorPosition,sizeof(fakeActorPosition));
  std::memcpy(fakeSpawn.data()+0x101c,&actorPointer,sizeof(actorPointer));
  std::memcpy(fakeSpawn.data()+0x148,&fakeId,4);
  D3DMATRIX projection{}; projection._11=projection._22=projection._44=1; projection._33=.1f;
  std::memcpy(fakeRender.data()+0x16c0,&projection,sizeof(projection));
  HudSpawn previewSpawn;
  previewSpawn.object=fakeSpawn.data(); previewSpawn.id=fakeId;
  previewSpawn.hp=75; previewSpawn.hpMax=100;
  previewSpawn.mana=45; previewSpawn.manaMax=100;
  previewSpawn.stamina=80; previewSpawn.staminaMax=100;
  previewSpawn.position={-100,150,0}; previewSpawn.heading=128;
  hudSpawns={previewSpawn};
  auto pet=previewSpawn; pet.id=322; pet.owner=321; pet.object=nullptr;
  pet.position={200,60,0}; pet.heading=384; hudSpawns.push_back(pet);
  auto otherPlayer=previewSpawn; otherPlayer.id=323; otherPlayer.object=nullptr;
  otherPlayer.position={-150,180,0}; otherPlayer.heading=64; hudSpawns.push_back(otherPlayer);
  std::array<BYTE,0x1200> npcMemory{};
  std::array<BYTE,0x200> npcActor{};
  Point npcPosition{.55f,-.5f,0};
  std::memcpy(npcActor.data()+0x114,&npcPosition,sizeof(npcPosition));
  void* npcActorPointer=npcActor.data(); std::uint32_t npcId=654;
  std::memcpy(npcMemory.data()+0x101c,&npcActorPointer,sizeof(npcActorPointer));
  std::memcpy(npcMemory.data()+0x148,&npcId,4);
  auto npc=previewSpawn; npc.object=npcMemory.data(); npc.id=npcId;
  npc.type=1; npc.owner=321; npc.position={100,100,0}; hudSpawns.push_back(npc);
  cfg.barWidths=page==2?std::array<float,4>{96,96,96,96}:std::array<float,4>{48,48,48,48};cfg.barHeights=page==2?std::array<float,4>{8,8,8,8}:std::array<float,4>{4,4,4,4};cfg.barRpg=preview==9;
  if(preview==9){cfg.barWidths={96,72,60,80};cfg.barHeights={10,6,4,7};cfg.npcHpColor=0xff55ddd0;}
  cfg.npcHpColor=preview==9?0xff55ddd0:page==2?0xffff5555:0xff35d85b;
  cfg.hpColor=page==2?0xffff5555:0xff35d85b;cfg.manaColor=page==2?0xffffffff:0xff468dff;cfg.staminaColor=page==2?0xffb68bff:0xffffd45e;
  DrawOverheadBars(fakeRender.data(),viewport,player);
  DrawMinimap(player, viewport);
  DrawMenu(viewport);
  canvas.Text(mapCX, mapCY + mapR + 11, "3:24 PM", TextColor, 0, 1, true);
  canvas.Text(780, 400, "125", 0xffffcc5c, 2, 1, true);
  canvas.Text(780, 350, "+15%", 0xff35ef69, 1, 1, true);
  canvas.Flush();
  check(SUCCEEDED(device->EndScene()));
  // Read the rendered pixels back before presentation, validating the real
  // D3D9 font atlas, batching, and primitive calls used by the DLL.
  IDirect3DSurface9 *back = nullptr;
  IDirect3DSurface9 *sys = nullptr;
  check(SUCCEEDED(device->GetRenderTarget(0, &back)));
  D3DSURFACE_DESC desc{};
  back->GetDesc(&desc);
  check(SUCCEEDED(device->CreateOffscreenPlainSurface(
      desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)));
  check(SUCCEEDED(device->GetRenderTargetData(back, sys)));
  D3DLOCKED_RECT locked{};
  check(SUCCEEDED(sys->LockRect(&locked, nullptr, D3DLOCK_READONLY)));
  auto pixel=[&](unsigned x,unsigned y) {
    std::uint32_t value=0;
    std::memcpy(&value,static_cast<char*>(locked.pBits)+y*locked.Pitch+x*4,4);
    return value;
  };
  const auto npcHp=pixel(690,490),npcMana=pixel(690,497),npcStamina=pixel(690,504);
  if(preview==9){
    check((pixel(690,491)&0xffffff)==0x55ddd0);check((pixel(720,491)&0xffffff)!=0x55ddd0);
    check((pixel(780,315)&0xffffff)==0xff5555);check((pixel(780,333)&0xffffff)==0xffffff);check((pixel(780,346)&0xffffff)==0xb68bff);
    check((pixel(746,315)&0xffffff)==0xff5555 && (pixel(746,333)&0xffffff)!=0xffffff);
    unsigned bronze=0;for(unsigned yy=308;yy<356;++yy)for(unsigned xx=730;xx<845;++xx){auto c=pixel(xx,yy);unsigned r=(c>>16)&255,g=(c>>8)&255,b=c&255;if(r>70 && r>g && g>b && b<120)++bronze;}check(bronze>80);
  } else if(page==2){
    check((pixel(780,315)&0xffffff)==0xff5555);check((pixel(750,315)&0xffffff)==0xff5555);
    check((pixel(780,320)&0xffffff)==0xff5555);check((pixel(780,326)&0xffffff)==0xffffff);check((pixel(780,337)&0xffffff)==0xb68bff);
  } else {
  check(((npcHp>>8)&255)>180);
  check(((npcMana>>8)&255)<100 && (npcMana&255)<100);
  check(((npcStamina>>16)&255)<100 && ((npcStamina>>8)&255)<100);
  const auto hpPixel=pixel(780,315), manaPixel=pixel(780,322), staminaPixel=pixel(780,329);
  check(((hpPixel>>8)&255)>180 && ((hpPixel>>16)&255)<100);
  check((manaPixel&255)>220 && ((manaPixel>>8)&255)>100);
  check(((staminaPixel>>16)&255)>150 && ((staminaPixel>>8)&255)>150 && (staminaPixel&255)<150);
  }
  BITMAPFILEHEADER file{};
  BITMAPINFOHEADER info{};
  file.bfType = 0x4d42;
  file.bfOffBits = sizeof(file) + sizeof(info);
  file.bfSize = file.bfOffBits + desc.Width * desc.Height * 4;
  info.biSize = sizeof(info);
  info.biWidth = desc.Width;
  info.biHeight = -LONG(desc.Height);
  info.biPlanes = 1;
  info.biBitCount = 32;
  info.biCompression = BI_RGB;
  std::string filename=preview==12?"gusto-hud-map-controller-preview.bmp":preview==13?"gusto-hud-map-keyboard-preview.bmp":preview==9?"gusto-hud-rpg-bars-preview.bmp":preview==10?"gusto-hud-gamepad-device-preview.bmp":preview==11?"gusto-hud-gamepad-picker-preview.bmp":page==0 ? "gusto-hud-preview.bmp" :
    "gusto-hud-preview-"+std::to_string(page)+".bmp";
  std::ofstream out(filename, std::ios::binary);
  out.write(reinterpret_cast<char *>(&file), sizeof(file));
  out.write(reinterpret_cast<char *>(&info), sizeof(info));
  for (unsigned y = 0; y < desc.Height; ++y)
    out.write(static_cast<char *>(locked.pBits) + y * locked.Pitch,
              desc.Width * 4);
  out.close();
  sys->UnlockRect();
  if(preview==9){
    check(SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff253544,1,0)));
    check(SUCCEEDED(device->BeginScene()));canvas.Begin();
    canvas.Depth(true,.05f);device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
    canvas.Rect(650,480,50,30,0xff112233);canvas.Flush();
    device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);canvas.Depth(false);
    DrawOverheadBars(fakeRender.data(),viewport,player,1);canvas.Flush();
    // Reproduce the late UI stage losing world depth: NPCs must not be
    // redrawn here, or they would become visible through the occluder.
    check(SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0)));
    DrawOverheadBars(fakeRender.data(),viewport,player,2);
    canvas.Rect(657,487,6,6,0xffabcdef);canvas.Flush();
    check(SUCCEEDED(device->EndScene()));check(SUCCEEDED(device->GetRenderTargetData(back,sys)));
    check(SUCCEEDED(sys->LockRect(&locked,nullptr,D3DLOCK_READONLY)));
    check((pixel(690,491)&0xffffff)==0x112233); // mesh hides NPC bar
    check((pixel(710,491)&0xffffff)==0x55ddd0); // unobstructed bar remains
    check((pixel(780,315)&0xffffff)==0xff5555); // player stays an overlay
    check((pixel(660,490)&0xffffff)==0xabcdef); // UI depth restored
    sys->UnlockRect();
  }
  sys->Release();
  back->Release();
  // All page buttons must stay inside the menu and window.
  for (const auto& b:buttons) {
    if (b.action==1 || b.action==2) continue; // minimap ring controls
    check(b.x>=menuX-1 && b.y>=menuY-1 && b.x+b.w<=menuX+menuW+1 && b.y+b.h<=menuY+menuH+1);
  }
  }
  // Actual RoF2 runtime-type decoding: ordinary focused buttons must not
  // block controller input after assigning a key. Editable controls still do.
  std::array<BYTE,0x58> focusedControl{};
  unsigned runtimeTypes[4]={0,17,0,0};void* runtimeBlocks[]={runtimeTypes};
  int typeCount=2,typeShift=2;unsigned typeMask=3;void* typeBlocks=runtimeBlocks;
  std::memcpy(focusedControl.data()+0x44,&typeCount,4);
  std::memcpy(focusedControl.data()+0x4c,&typeMask,4);
  std::memcpy(focusedControl.data()+0x50,&typeShift,4);
  std::memcpy(focusedControl.data()+0x54,&typeBlocks,4);
  bool editing=true;
  check(FocusedWindowEditsText(focusedControl.data(),editing) && !editing && !pad::KeyboardTypingGate(1,editing));
  runtimeTypes[1]=2;check(FocusedWindowEditsText(focusedControl.data(),editing) && editing && pad::KeyboardTypingGate(1,editing));
  runtimeTypes[1]=8;check(FocusedWindowEditsText(focusedControl.data(),editing) && editing);
  typeCount=65;std::memcpy(focusedControl.data()+0x44,&typeCount,4);
  check(!FocusedWindowEditsText(focusedControl.data(),editing));
  check(FocusedWindowEditsText(nullptr,editing) && !editing);
  // A mouse-capture transfer must not cancel keyboard/controller holds or
  // the mapping wizard. Actual keyboard focus loss still cancels capture.
  padKeyListening=true;BeginGamepadKeyboardCapture();
  ownedLeft=dragging=true;
  CancelHudInput(WM_CAPTURECHANGED);
  check(!ownedLeft && !dragging && padKeyListening);
  FeedGamepadKeyboardCapture('W',true);
  check(ConsumeGamepadKeyboardCapture()=='W');
  BeginGamepadKeyboardCapture();padKeyListening=true;
  CancelHudInput(WM_KILLFOCUS);
  check(!padKeyListening && ConsumeGamepadKeyboardCapture()==-3);
  DrainGamepadKeyboardCapture(true);
  for(int button=0;button<3;++button){
    padInput=button;padKeyListening=true;menuPage=7;menu=true;
    BeginGamepadKeyboardCapture();FeedGamepadKeyboardCapture(button==0?VK_LBUTTON:button==1?VK_RBUTTON:VK_MBUTTON,true);
    device->BeginScene();canvas.Begin();DrawMenu({0,0,900,700,0,1});canvas.Flush();device->EndScene();
    check(GetGamepadOptions().bindings[button]==pad::MouseLeft+button && !padKeyListening);
    FeedGamepadKeyboardCapture(button==0?VK_LBUTTON:button==1?VK_RBUTTON:VK_MBUTTON,false);DrainGamepadKeyboardCapture(true);
  }
  // Finish the real second-stage UI and verify commit and cancellation.
  padInput=0;padKeyListening=true;padListening=padPicker=padDevicePage=false;menuPage=7;
  BeginGamepadKeyboardCapture();FeedGamepadKeyboardCapture(VK_CONTROL,true);FeedGamepadKeyboardCapture('L',true);
  device->BeginScene();canvas.Begin();buttons.clear();DrawMenu({0,0,900,700,0,1});canvas.Flush();device->EndScene();
  check(GetGamepadOptions().bindings[0]==(pad::Control|'L') && !padKeyListening && padBindingFeedback.starts_with("Saved:"));
  padKeyListening=true;BeginGamepadKeyboardCapture();FeedGamepadKeyboardCapture(VK_ESCAPE,true);
  device->BeginScene();canvas.Begin();buttons.clear();DrawMenu({0,0,900,700,0,1});canvas.Flush();device->EndScene();
  check(GetGamepadOptions().bindings[0]==(pad::Control|'L') && !padKeyListening);
  CancelGamepadInputCapture();CancelGamepadKeyboardCapture();DrainGamepadKeyboardCapture(true);padListening=padKeyListening=false;
  hudSpawns.push_back({}); hudSpawns.back().id=777;
  previousHealth.emplace(777,hud::HealthSample{123,50,100});
  hud::FloatEffect oldEffect; oldEffect.target=777; effects.push_back(oldEffect);
  RemoveHudSpawn(777);
  check(!previousHealth.contains(777));
  check(std::none_of(hudSpawns.begin(),hudSpawns.end(),[](const auto& e){return e.id==777;}));
  check(std::none_of(effects.begin(),effects.end(),[](const auto& e){return e.target==777;}));
  // A genuine independent top-level window renders the same full map data.
  ExternalMapState detached;
  detached.map=std::make_shared<const hud::Map>(map);detached.zone="Plane of Knowledge";
  detached.markers.push_back({{150,100,0},128,0xffc9a477});
  UpdateExternalMap(detached);check(OpenExternalMap());
  for(int i=0;i<200 && !ExternalMapWindow();++i) Sleep(10);
  auto external=ExternalMapWindow();check(external!=nullptr);
  check(GetParent(external)==nullptr && !(GetWindowLongPtrW(external,GWL_STYLE)&WS_CHILD));
  check(SetWindowPos(external,nullptr,-140,-60,820,640,SWP_NOACTIVATE|SWP_NOZORDER));
  RECT outside{};GetWindowRect(external,&outside);check(outside.left<0);
  RECT client{};GetClientRect(external,&client);check(client.right>700 && client.bottom>500);
  auto dc=GetDC(external);auto buffer=CreateCompatibleDC(dc);
  BITMAPINFO externalInfo{};externalInfo.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  externalInfo.bmiHeader.biWidth=client.right;externalInfo.bmiHeader.biHeight=-client.bottom;
  externalInfo.bmiHeader.biPlanes=1;externalInfo.bmiHeader.biBitCount=32;externalInfo.bmiHeader.biCompression=BI_RGB;
  void* externalPixels=nullptr;
  auto externalBitmap=CreateDIBSection(dc,&externalInfo,DIB_RGB_COLORS,&externalPixels,nullptr,0);
  check(buffer && externalBitmap && externalPixels);
  auto previous=SelectObject(buffer,externalBitmap);
  auto printMap=[&](){SendMessageW(external,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);};
  auto centerColor=[&](){return static_cast<std::uint32_t*>(externalPixels)[((client.bottom+12)/2)*client.right+client.right/2]&0xffffff;};
  printMap();check(centerColor()==0x7ac7ff);
  SendMessageW(external,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(300,300));
  SendMessageW(external,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(350,330));
  SendMessageW(external,WM_LBUTTONUP,0,MAKELPARAM(350,330));
  printMap();check(centerColor()!=0x7ac7ff);
  SendMessageW(external,WM_KEYDOWN,VK_SPACE,0);printMap();check(centerColor()==0x7ac7ff);
  SendMessageW(external,WM_MOUSEWHEEL,MAKEWPARAM(0,120),0);printMap();
  check(centerColor()==0x7ac7ff);
  BITMAPFILEHEADER externalFile{};externalFile.bfType=0x4d42;
  externalFile.bfOffBits=sizeof(externalFile)+sizeof(BITMAPINFOHEADER);
  externalFile.bfSize=externalFile.bfOffBits+client.right*client.bottom*4;
  std::ofstream imageOut("gusto-external-map-preview.bmp",std::ios::binary);
  imageOut.write(reinterpret_cast<char*>(&externalFile),sizeof(externalFile));
  imageOut.write(reinterpret_cast<char*>(&externalInfo.bmiHeader),sizeof(BITMAPINFOHEADER));
  imageOut.write(static_cast<char*>(externalPixels),client.right*client.bottom*4);imageOut.close();
  SelectObject(buffer,previous);DeleteObject(externalBitmap);DeleteDC(buffer);ReleaseDC(external,dc);
  CloseExternalMap();for(int i=0;i<200 && ExternalMapRunning();++i)Sleep(10);
  check(!ExternalMapRunning() && !ExternalMapWindow());
  canvas.Release();
  device->Release();
  api->Release();
  DestroyWindow(hwnd);
  std::printf(
      "PASS: independent external map resize/move/pan/zoom/close; DirectInput; profile mana/endurance; settings persistence; nine D3D9 menu pages; player/pet arrows, resource bars and text rendered/read back\n");
}
