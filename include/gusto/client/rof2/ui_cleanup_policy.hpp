#pragma once
#include <array>
namespace gusto::client::rof2 {
inline constexpr std::array InventoryHiddenControls{
  "RadiantShardsLabel", "EbonShardsLabel", "IW_GoodPoints", "IW_EvilPoints",
  "IW_ReclaimButton", "IW_PlotItems", "InvSlot21"
};
inline constexpr std::array InventoryRemovedPages{"EIPage", "AltCharProgPage", "AltCurrPage"};
inline constexpr std::array EqMainHiddenControls{"EQButton", "SCButton", "SCFSButton"};
}
