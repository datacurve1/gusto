#include "gusto/client/rof2/client_state.hpp"

#include "gusto/client/rof2/addresses.hpp"
#include "gusto/client/rof2/globals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace gusto::client::rof2
{
    namespace
    {
        bool IsReadableRange(const void* address, std::size_t length)
        {
            if (!address || length == 0)
                return false;

            const auto start =
                reinterpret_cast<std::uintptr_t>(address);

            if (length >
                std::numeric_limits<std::uintptr_t>::max() - start)
                return false;

            const auto end = start + length;
            auto current = start;

            while (current < end)
            {
                MEMORY_BASIC_INFORMATION info{};

                if (!VirtualQuery(
                    reinterpret_cast<const void*>(current),
                    &info,
                    sizeof(info)))
                    return false;

                if (info.State != MEM_COMMIT ||
                    (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                    return false;

                const DWORD protection = info.Protect & 0xFF;

                const bool readable =
                    protection == PAGE_READONLY ||
                    protection == PAGE_READWRITE ||
                    protection == PAGE_WRITECOPY ||
                    protection == PAGE_EXECUTE_READ ||
                    protection == PAGE_EXECUTE_READWRITE ||
                    protection == PAGE_EXECUTE_WRITECOPY;

                if (!readable)
                    return false;

                const auto regionBase =
                    reinterpret_cast<std::uintptr_t>(info.BaseAddress);

                if (info.RegionSize >
                    std::numeric_limits<std::uintptr_t>::max() - regionBase)
                    return false;

                const auto regionEnd = regionBase + static_cast<std::uintptr_t>(info.RegionSize);

                if (regionEnd <= current)
                    return false;

                current = (std::min)(regionEnd, end);
            }

            return true;
        }

        template <typename T>
        bool ReadField(
            const void* object,
            std::size_t offset,
            T& value)
        {
            if (!object)
                return false;

            const auto base =
                reinterpret_cast<std::uintptr_t>(object);

            if (offset >
                std::numeric_limits<std::uintptr_t>::max() - base)
                return false;

            const auto address = base + offset;
            const auto* source =
                reinterpret_cast<const void*>(address);

            if (!IsReadableRange(source, sizeof(T)))
                return false;

            std::memcpy(&value, source, sizeof(T));
            return true;
        }

        template <std::size_t N>
        bool ReadStringField(
            const void* object,
            std::size_t offset,
            char (&destination)[N])
        {
            destination[0] = '\0';

            if (!object || N == 0)
                return false;

            const auto base =
                reinterpret_cast<std::uintptr_t>(object);

            if (offset >
                std::numeric_limits<std::uintptr_t>::max() - base)
                return false;

            const auto address = base + offset;

            for (std::size_t i = 0; i < N; ++i)
            {
                if (i >
                    std::numeric_limits<std::uintptr_t>::max() - address)
                    return false;

                const auto* source =
                    reinterpret_cast<const void*>(address + i);

                char value = '\0';

                if (!IsReadableRange(source, sizeof(value)))
                    return false;

                std::memcpy(&value, source, sizeof(value));

                destination[i] = value;

                if (value == '\0')
                    return true;
            }

            destination[0] = '\0';
            return false;
        }

        bool ReadFiniteFloat(
            const void* object,
            std::size_t offset,
            float& value)
        {
            float candidate = 0.0f;

            if (!ReadField(object, offset, candidate) ||
                !std::isfinite(candidate))
                return false;

            value = candidate;
            return true;
        }
    }

    ClientState::ClientState(
        HMODULE module,
        const ValidatedClient& validation)
        : m_module(module),
          m_validation(validation)
    {
    }

    bool ClientState::IsValid() const
    {
        return m_validation.IsValidFor(m_module);
    }

    EverQuestInfo* ClientState::GetEverQuestInfo() const
    {
        return static_cast<EverQuestInfo*>(
            ResolveObject(
                m_module, m_validation,
                addresses::EverQuestInfo,
                addresses::EverQuestInfoReferenceSize));
    }

    ZoneInfo* ClientState::GetZoneInfo() const
    {
        return static_cast<ZoneInfo*>(
            ResolveObject(
                m_module, m_validation,
                addresses::ZoneInfo,
                addresses::ZoneInfoReferenceSize));
    }

    PlayerClient* ClientState::GetLocalPlayer() const
    {
        return static_cast<PlayerClient*>(
            ResolvePointer(
                m_module, m_validation,
                addresses::LocalPlayer,
                addresses::PlayerClientReferenceSize));
    }

    PlayerClient* ClientState::GetControlledPlayer() const
    {
        return static_cast<PlayerClient*>(
            ResolvePointer(
                m_module, m_validation,
                addresses::ControlledPlayer,
                addresses::PlayerClientReferenceSize));
    }

    PlayerClient* ClientState::GetTarget() const
    {
        return static_cast<PlayerClient*>(
            ResolvePointer(
                m_module, m_validation,
                addresses::Target,
                addresses::PlayerClientReferenceSize));
    }

    PlayerClient* ClientState::GetTradeTarget() const
    {
        return static_cast<PlayerClient*>(
            ResolvePointer(
                m_module, m_validation,
                addresses::TradeTarget,
                addresses::PlayerClientReferenceSize));
    }

    bool ClientState::GetPlayerSnapshot(
        PlayerSnapshot& snapshot) const
    {
        snapshot = {};

        if (!IsValid())
            return false;

        const auto* player = GetLocalPlayer();

        if (!player)
            return false;

        snapshot.available = true;

        snapshot.nameValid =
            ReadStringField(player, 0x0A4, snapshot.name);

        snapshot.displayedNameValid =
            ReadStringField(player, 0x0E4, snapshot.displayedName);

        snapshot.levelValid =
            ReadField(player, 0x250, snapshot.level);

        const bool yValid =
            ReadFiniteFloat(player, 0x064, snapshot.y);

        const bool xValid =
            ReadFiniteFloat(player, 0x068, snapshot.x);

        const bool zValid =
            ReadFiniteFloat(player, 0x06C, snapshot.z);

        snapshot.positionValid = yValid && xValid && zValid;

        snapshot.headingValid =
            ReadFiniteFloat(player, 0x080, snapshot.heading);

        return true;
    }

    bool ClientState::GetZoneSnapshot(
        ZoneSnapshot& snapshot) const
    {
        snapshot = {};

        if (!IsValid())
            return false;

        const auto* zone = GetZoneInfo();

        if (!zone)
            return false;

        snapshot.available = true;

        snapshot.characterNameValid =
            ReadStringField(zone, 0x000, snapshot.characterName);

        snapshot.shortNameValid =
            ReadStringField(zone, 0x040, snapshot.shortName);

        snapshot.longNameValid =
            ReadStringField(zone, 0x0C0, snapshot.longName);

        snapshot.zoneIdValid =
            ReadField(zone, 0x354, snapshot.zoneId);

        return true;
    }
}
