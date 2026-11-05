#pragma once

#include <windows.h>
#include <cstdint>

#include "gusto/client/rof2/compatibility.hpp"

namespace gusto::client::rof2
{
    struct PlayerClient;
    struct EverQuestInfo;
    struct ZoneInfo;

    struct PlayerSnapshot
    {
        bool available = false;
        bool nameValid = false;
        bool displayedNameValid = false;
        bool levelValid = false;
        bool positionValid = false;
        bool headingValid = false;

        char name[64]{};
        char displayedName[64]{};

        std::uint8_t level = 0;
        float y = 0.0f;
        float x = 0.0f;
        float z = 0.0f;
        float heading = 0.0f;
    };

    struct ZoneSnapshot
    {
        bool available = false;
        bool characterNameValid = false;
        bool shortNameValid = false;
        bool longNameValid = false;
        bool zoneIdValid = false;

        char characterName[64]{};
        char shortName[128]{};
        char longName[128]{};

        std::uint32_t zoneId = 0;
    };

    class ClientState
    {
    public:
        ClientState(
            HMODULE module,
            const ValidatedClient& validation);

        bool IsValid() const;

        EverQuestInfo* GetEverQuestInfo() const;
        ZoneInfo* GetZoneInfo() const;

        PlayerClient* GetLocalPlayer() const;
        PlayerClient* GetControlledPlayer() const;
        PlayerClient* GetTarget() const;
        PlayerClient* GetTradeTarget() const;

        bool GetPlayerSnapshot(PlayerSnapshot& snapshot) const;
        bool GetZoneSnapshot(ZoneSnapshot& snapshot) const;

    private:
        HMODULE m_module = nullptr;
        ValidatedClient m_validation{};
    };
}
