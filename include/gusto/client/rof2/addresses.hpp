#pragma once

#include <cstdint>
#include <cstddef>

namespace gusto::client::rof2::addresses
{
    inline constexpr std::uintptr_t PreferredImageBase = 0x00400000;

    // Address interpretation is important:
    // DirectObject: the address itself is the object.
    // PointerStorage: the address contains a pointer to the object.
    enum class Storage
    {
        DirectObject,
        PointerStorage
    };

    struct Global
    {
        std::uintptr_t preferredVA;
        Storage storage;

        constexpr std::uintptr_t RVA() const
        {
            return preferredVA - PreferredImageBase;
        }
    };

    // Direct object globals.
    inline constexpr Global EverQuestInfo{
        0x00DDF618, Storage::DirectObject
    };

    inline constexpr Global ZoneInfo{
        0x00DDF8F0, Storage::DirectObject
    };

    // Pointer-storage globals.
    inline constexpr Global LocalPlayer{
        0x00DD2630, Storage::PointerStorage
    };

    inline constexpr Global ControlledPlayer{
        0x00DD2644, Storage::PointerStorage
    };

    inline constexpr Global Target{
        0x00DD2648, Storage::PointerStorage
    };

    inline constexpr Global TradeTarget{
        0x00DD262C, Storage::PointerStorage
    };

    inline constexpr Global Display{
        0x00DD2660, Storage::PointerStorage
    };

    inline constexpr Global SidlManager{
        0x015D3D08, Storage::PointerStorage
    };

    inline constexpr Global WindowManager{
        0x015D3D00, Storage::PointerStorage
    };

    // Documented relationship:
    // EverQuestInfo::ZoneInfo is at offset 0x2D8.
    inline constexpr std::uintptr_t ZoneInfoOffset = 0x2D8;

    static_assert(
        ZoneInfo.preferredVA - EverQuestInfo.preferredVA == ZoneInfoOffset,
        "RoF2 EverQuestInfo/ZoneInfo address relationship changed."
    );

    // Sizes documented by the RoF2 reference source.
    // These are not independently runtime-validated by GUSTO.
    inline constexpr std::size_t EverQuestInfoReferenceSize = 0x83F9C;
    inline constexpr std::size_t ZoneInfoReferenceSize = 0x3B4;
    inline constexpr std::size_t DisplayReferenceSize = 0x2D6B;
    inline constexpr std::size_t PlayerClientReferenceSize = 0x1F08;
}
