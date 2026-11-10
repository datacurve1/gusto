#include "gusto/client/rof2/life_fx.hpp"
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cmath>
using namespace gusto::client::rof2;
void Check(bool okay,const char* text){if(!okay){std::printf("FAIL: %s\n",text);std::exit(1);}}
int main(){
  WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"GustoLifePreview";RegisterClassW(&wc);
  auto hwnd=CreateWindowW(wc.lpszClassName,L"Gusto death and feign models",WS_OVERLAPPEDWINDOW,0,0,1100,760,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(hwnd,SW_SHOW);
  auto* api=Direct3DCreate9(D3D_SDK_VERSION);Check(api!=nullptr,"create D3D");
  D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=1100;pp.BackBufferHeight=700;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.hDeviceWindow=hwnd;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D16;
  IDirect3DDevice9* device=nullptr;Check(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device)),"create device");
  SetLifeOptions({});ResetLifeEffects();
  for(int i=0;i<3;++i){
    life::Sample sample{std::uint32_t(i+1),std::uintptr_t(i+1),std::uint8_t(i==0?1:0),0x64,100,100,{float((i-1)*4.4f),0,0},4,true};
    Check(!ObserveLifeSample(sample,1000,true),"first observation");
    sample.stand=i==2?0x73:0x78;if(i!=2)sample.hp=0;
    const bool laugh=ObserveLifeSample(sample,1100,true);Check(laugh==(i==2),"only FD requests the one-shot laugh");
    Check(!ObserveLifeSample(sample,1200,true),"repeated pose has no sound");
  }
  Check(LifeEffectCount()==3,"three model kinds queued");
  auto config=GetLifeOptions();config.rise=.5f;SetLifeOptions(config);
  D3DMATRIX vp{};vp._11=.11f;vp._23=.04f;vp._32=.11f*1100/700;vp._42=-.23f;vp._43=.5f;vp._44=1;
  device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff152032,1,0);
  Check(SUCCEEDED(device->BeginScene()),"begin");
  device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
  Check(SUCCEEDED(DrawLifeEffects(device,vp,{0,-10,3},2600)),"draw real Blender meshes");
  DWORD blend=1,zwrite=0;device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);device->GetRenderState(D3DRS_ZWRITEENABLE,&zwrite);
  Check(!blend && zwrite,"renderer restores native state");
  device->EndScene();
  IDirect3DSurface9 *back=nullptr,*copy=nullptr;device->GetRenderTarget(0,&back);D3DSURFACE_DESC desc{};back->GetDesc(&desc);
  Check(SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr)),"readback surface");
  Check(SUCCEEDED(device->GetRenderTargetData(back,copy)),"model readback");D3DLOCKED_RECT locked{};copy->LockRect(&locked,nullptr,D3DLOCK_READONLY);
  unsigned white=0,orange=0,blue=0;
  for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x){auto c=reinterpret_cast<std::uint32_t*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x];unsigned r=(c>>16)&255,g=(c>>8)&255,b=c&255;
    if(x<400 && r>130 && g>130 && b>130)++white;
    if(x>350 && x<750 && r>140 && r>g*1.4 && g>b*1.4)++orange;
    if(x>700 && b>100 && b>r*1.3)++blue;
  }
  Check(white>1000 && orange>2000 && blue>1000,"white NPC, larger orange player, and blue jester pixels");
  BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+desc.Width*desc.Height*4;
  info.biSize=sizeof(info);info.biWidth=desc.Width;info.biHeight=-LONG(desc.Height);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
  std::ofstream output("gusto-life-effects-preview.bmp",std::ios::binary);output.write(reinterpret_cast<char*>(&file),sizeof(file));output.write(reinterpret_cast<char*>(&info),sizeof(info));
  for(unsigned y=0;y<desc.Height;++y)output.write(static_cast<char*>(locked.pBits)+y*locked.Pitch,desc.Width*4);output.close();copy->UnlockRect();copy->Release();back->Release();
  // Changing independent colors and scales affects already active spirits.
  config.npcColor=0xff65ed96;config.playerColor=0xffb68bff;config.npcScale=2;config.playerScale=1.5f;SetLifeOptions(config);
  device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff152032,1,0);device->BeginScene();
  Check(SUCCEEDED(DrawLifeEffects(device,vp,{0,-10,3},2600)),"custom ghost colors and sizes");device->EndScene();
  device->GetRenderTarget(0,&back);device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr);
  Check(SUCCEEDED(device->GetRenderTargetData(back,copy)),"custom colors readback");copy->LockRect(&locked,nullptr,D3DLOCK_READONLY);
  unsigned green=0,purple=0;
  for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x){auto c=reinterpret_cast<std::uint32_t*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x];unsigned r=(c>>16)&255,g=(c>>8)&255,b=c&255;
   if(x<400 && g>120 && g>r*1.4 && g>b*1.2)++green;
   if(x>350 && x<750 && b>140 && r>100 && b>g*1.3 && r>g*1.1)++purple;
  }
  Check(green>white && purple>500 && purple<orange,"independent colors and larger NPC / smaller player sizes change pixels");
  copy->UnlockRect();copy->Release();back->Release();
  DrawLifeEffects(device,vp,{0,-10,3},10000);Check(LifeEffectCount()==0,"expired effects removed");
  config.feign=false;SetLifeOptions(config);life::Sample disabled{10,10,0,0x64,100,100,{0,0,0},4,true};ObserveLifeSample(disabled,11000,true);disabled.stand=0x73;
  Check(!ObserveLifeSample(disabled,11100,true) && !LifeEffectCount(),"disabled jester is silent and hidden");
  ResetLifeEffects();SetLifeOptions({});life::Sample distant{11,11,0,0x64,100,100,{0,0,0},4,true};ObserveLifeSample(distant,12000,false);distant.stand=0x73;
  Check(!ObserveLifeSample(distant,12100,false)&&!LifeEffectCount(),"distant FD has no effects or sound");
  device->Release();api->Release();DestroyWindow(hwnd);
  std::printf("PASS: three Blender meshes rendered in D3D9; colors, state restoration, expiry, toggles and sound gating\n");
}
