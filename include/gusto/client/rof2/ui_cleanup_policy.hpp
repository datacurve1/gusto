#pragma once
#include <array>
namespace gusto::client::rof2 {
inline constexpr std::array InventoryHiddenControls{
  "RadiantShardsLabel", "EbonShardsLabel", "IW_GoodPoints", "IW_EvilPoints",
  "IW_ReclaimButton", "IW_PlotItems", "InvSlot21"
};
inline constexpr std::array InventoryRemovedPages{"EIPage", "AltCharProgPage", "AltCurrPage"};
inline constexpr std::array EqMainHiddenControls{"EQButton", "SCButton", "SCFSButton"};
inline constexpr std::array CharacterSelectHiddenControls{
  "Buy_Slot_Button", "CLW_GoldMemberAnim", "CLW_FreeGold_Button", "CLW_SilverGold_Button"
};
inline constexpr std::array CharacterCreateHiddenControls{
  "CC_Buy_Race_Button", "CC_Buy_Class_Button", "CC_Race_Drakkin_Button",
  "CC_Race_Drakkin_Button_Male", "CC_Race_Drakkin_Overlay_Button"
};
}
