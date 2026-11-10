// Independent full-map window. Only immutable map data and value snapshots
// cross the thread boundary; this thread never reads client memory or D3D.
#include "gusto/client/rof2/hud_external_map.hpp"
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>
namespace gusto::client::rof2 {
namespace {
SRWLOCK stateLock=SRWLOCK_INIT;
ExternalMapState latest;
std::atomic<HWND> mapWindow{nullptr};
std::atomic_bool starting{false}, closing{false};
constexpr wchar_t ClassName[]=L"GustoExternalZoneMap";
struct View {
  float range=1200,panX=0,panY=0;
  POINT last{};
  bool dragging=false;
  std::string zone;
  HFONT font=nullptr;
};
ExternalMapState Snapshot() {
  AcquireSRWLockShared(&stateLock);
  auto copy=latest;
  ReleaseSRWLockShared(&stateLock);
  return copy;
}
COLORREF Color(std::uint32_t c,float alpha=1) {
  auto blend=[&](int shift,int bg){return BYTE(((c>>shift)&255)*alpha+bg*(1-alpha));};
  return RGB(blend(16,14),blend(8,17),blend(0,23));
}
void Paint(HDC dc,RECT area,View& view) {
  auto state=Snapshot();
  if (view.zone!=state.zone) { view.zone=state.zone; view.panX=view.panY=0; }
  const int width=area.right-area.left,height=area.bottom-area.top;
  auto brush=CreateSolidBrush(RGB(14,17,23)); FillRect(dc,&area,brush); DeleteObject(brush);
  auto oldFont=SelectObject(dc,view.font?view.font:GetStockObject(DEFAULT_GUI_FONT));
  SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(237,242,250));
  const std::string title=state.zone.empty()?"Waiting for a zone map":state.zone;
  TextOutA(dc,14,9,title.c_str(),int(title.size()));
  const char help[]="Wheel: zoom   Drag: pan   Space / double-click: follow player";
  TextOutA(dc,14,std::max(0,height-25),help,int(sizeof(help)-1));
  if (width<80 || height<100) { SelectObject(dc,oldFont); return; }
  const float cx=width/2.f,cy=(height+12)/2.f;
  const float halfW=width/2.f-14,halfH=(height-80)/2.f;
  const float radius=std::max(1.f,std::min(halfW,halfH));
  hud::Point center{state.player.x+view.panX,state.player.y+view.panY,state.player.z};
  auto project=[&](hud::Point point){return hud::ProjectMap(point,center,0,radius,view.range,false);};
  const int saved=SaveDC(dc);
  IntersectClipRect(dc,10,36,width-10,height-36);
  SelectObject(dc,GetStockObject(DC_PEN)); SelectObject(dc,GetStockObject(DC_BRUSH));
  if (state.map) {
    for (const auto& line:state.map->lines) {
      if (!(state.layers&(1<<line.layer))) continue;
      auto a=project(line.a),b=project(line.b);
      if (!hud::ClipRect(a,b,halfW,halfH)) continue;
      auto color=(line.color&0xffffff)?line.color:0xffc6ccd6;
      SetDCPenColor(dc,Color(color,hud::HeightAlpha((line.a.z+line.b.z)/2,state.player.z,state.autoZ,state.above,state.below)));
      MoveToEx(dc,int(cx+a.x),int(cy+a.y),nullptr); LineTo(dc,int(cx+b.x),int(cy+b.y));
    }
    if (state.labels)
      for (const auto& label:state.map->labels) {
        if (!(state.layers&(1<<label.layer))) continue;
        auto p=project(label.p);
        if (!std::isfinite(p.x)||!std::isfinite(p.y)||std::fabs(p.x)>halfW||std::fabs(p.y)>halfH) continue;
        SetTextColor(dc,Color((label.color&0xffffff)?label.color:0xffedf2fa));
        TextOutA(dc,int(cx+p.x),int(cy+p.y),label.text.c_str(),int(label.text.size()));
      }
  }
  auto arrow=[&](hud::Point p,float heading,std::uint32_t color,float size) {
    if (!std::isfinite(p.x)||!std::isfinite(p.y)||std::fabs(p.x)>halfW||std::fabs(p.y)>halfH) return;
    const auto v=hud::HeadingArrow({cx+p.x,cy+p.y},heading,size*state.arrowScale);
    POINT points[3]; for (int i=0;i<3;++i) points[i]={LONG(v[i].x),LONG(v[i].y)};
    SetDCPenColor(dc,Color(color)); SetDCBrushColor(dc,Color(color)); Polygon(dc,points,3);
  };
  for (const auto& marker:state.markers) arrow(project(marker.mapPosition),marker.heading,marker.color,7);
  arrow(project({-state.player.x,-state.player.y,state.player.z}),state.heading,0xff7ac7ff,9);
  RestoreDC(dc,saved); SelectObject(dc,oldFont);
}
LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
  auto* view=reinterpret_cast<View*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  if (message==WM_NCCREATE) {
    view=new View;
    view->font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
    SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(view));
  }
  if (!view) return DefWindowProcW(hwnd,message,wp,lp);
  switch (message) {
  case WM_CREATE: SetTimer(hwnd,1,100,nullptr); return 0;
  case WM_TIMER: InvalidateRect(hwnd,nullptr,FALSE); return 0;
  case WM_ERASEBKGND:return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps{}; auto target=BeginPaint(hwnd,&ps); RECT rect{}; GetClientRect(hwnd,&rect);
    auto dc=CreateCompatibleDC(target);
    auto bitmap=CreateCompatibleBitmap(target,std::max(1,int(rect.right)),std::max(1,int(rect.bottom)));
    if (dc && bitmap) {
      auto old=SelectObject(dc,bitmap); Paint(dc,rect,*view);
      BitBlt(target,0,0,rect.right,rect.bottom,dc,0,0,SRCCOPY); SelectObject(dc,old);
    }
    if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc);
    EndPaint(hwnd,&ps); return 0;
  }
  case WM_PRINTCLIENT: {RECT rect{}; GetClientRect(hwnd,&rect); Paint(reinterpret_cast<HDC>(wp),rect,*view); GdiFlush(); return 0;}
  case WM_MOUSEWHEEL:
    view->range=std::clamp(view->range*std::pow(.8f,float(GET_WHEEL_DELTA_WPARAM(wp))/120),50.f,20000.f);
    InvalidateRect(hwnd,nullptr,FALSE);return 0;
  case WM_LBUTTONDOWN: view->dragging=true;view->last={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; SetCapture(hwnd);return 0;
  case WM_MOUSEMOVE:
    if (view->dragging) {
      RECT rect{};GetClientRect(hwnd,&rect);
      const float radius=std::max(1.f,std::min(rect.right/2.f-14,(rect.bottom-80)/2.f));
      view->panX+=(GET_X_LPARAM(lp)-view->last.x)*view->range/radius;
      view->panY+=(GET_Y_LPARAM(lp)-view->last.y)*view->range/radius;
      view->last={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};InvalidateRect(hwnd,nullptr,FALSE);
    }return 0;
  case WM_LBUTTONUP: view->dragging=false;if(GetCapture()==hwnd)ReleaseCapture();return 0;
  case WM_CAPTURECHANGED:view->dragging=false;return 0;
  case WM_LBUTTONDBLCLK: view->panX=view->panY=0;InvalidateRect(hwnd,nullptr,FALSE);return 0;
  case WM_KEYDOWN:if(wp==VK_SPACE){view->panX=view->panY=0;InvalidateRect(hwnd,nullptr,FALSE);return 0;}break;
  case WM_CLOSE:DestroyWindow(hwnd);return 0;
  case WM_DESTROY:KillTimer(hwnd,1);mapWindow=nullptr;PostQuitMessage(0);return 0;
  case WM_NCDESTROY:
    if(view->font)DeleteObject(view->font);delete view;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);break;
  }
  return DefWindowProcW(hwnd,message,wp,lp);
}
DWORD WINAPI MapThread(void* argument) {
  auto module=static_cast<HMODULE>(argument);
  WNDCLASSW wc{};wc.style=CS_DBLCLKS;wc.lpfnWndProc=&WindowProc;wc.hInstance=module;
  wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));wc.lpszClassName=ClassName;
  RegisterClassW(&wc);
  auto hwnd=CreateWindowExW(WS_EX_APPWINDOW,ClassName,L"Gusto - Zone Map",WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT,CW_USEDEFAULT,900,700,nullptr,nullptr,module,nullptr);
  mapWindow=hwnd;
  if(hwnd && !closing) {
    ShowWindow(hwnd,SW_SHOWNORMAL);UpdateWindow(hwnd);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
  } else if(hwnd) DestroyWindow(hwnd);
  mapWindow=nullptr;UnregisterClassW(ClassName,module);starting=false;
  // Keep the module loaded until its window procedure and thread have finished.
  FreeLibraryAndExitThread(module,0);
}
}
bool OpenExternalMap() {
  if(auto hwnd=mapWindow.load()){ShowWindowAsync(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);return true;}
  bool expected=false;if(!starting.compare_exchange_strong(expected,true))return true;
  closing=false;
  HMODULE module=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&MapThread),&module)){
    starting=false;return false;
  }
  HANDLE thread=CreateThread(nullptr,0,&MapThread,module,0,nullptr);
  if(!thread){starting=false;FreeLibrary(module);return false;}CloseHandle(thread);return true;
}
void CloseExternalMap(){closing=true;if(auto hwnd=mapWindow.load())PostMessageW(hwnd,WM_CLOSE,0,0);}
HWND ExternalMapWindow(){return mapWindow.load();}
bool ExternalMapRunning(){return starting.load();}
void UpdateExternalMap(ExternalMapState state) {
  AcquireSRWLockExclusive(&stateLock);latest=std::move(state);ReleaseSRWLockExclusive(&stateLock);
}
}
