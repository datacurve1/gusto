#pragma once
#include "gusto/client/rof2/hud_model.hpp"
#include <windows.h>
#include <memory>
namespace gusto::client::rof2 {
struct ExternalMapMarker {
  hud::Point mapPosition;
  float heading=0;
  std::uint32_t color=0xffffffff;
};
struct ExternalMapState {
  std::shared_ptr<const hud::Map> map;
  std::string zone;
  hud::Point player;
  float heading=0, arrowScale=1, above=30, below=30;
  bool labels=true, autoZ=true;
  int layers=15;
  std::vector<ExternalMapMarker> markers;
};
bool OpenExternalMap();
void CloseExternalMap();
HWND ExternalMapWindow();
bool ExternalMapRunning();
void UpdateExternalMap(ExternalMapState state);
}
