#include "gusto/client/rof2/diagnostics.hpp"

#include "gusto/client/rof2/compatibility.hpp"
#include "gusto/client/rof2/initialization.hpp"

#include <windows.h>
#include <cstdio>
#include <cwchar>

namespace
{
    constexpr int DiagnosticSuccess = 0;
    constexpr int DiagnosticValidationFailed = 1;
    constexpr int DiagnosticPlayerUnavailable = 2;
    constexpr int DiagnosticZoneUnavailable = 3;
    constexpr int DiagnosticLogFailure = 4;

    bool WriteDiagnosticLog(
        const gusto::client::rof2::ValidationReport& report,
        const gusto::client::rof2::PlayerSnapshot& player,
        const gusto::client::rof2::ZoneSnapshot& zone,
        bool playerAvailable,
        bool zoneAvailable)
    {
        wchar_t tempPath[MAX_PATH]{};

        const DWORD pathLength =
            GetTempPathW(MAX_PATH, tempPath);

        if (pathLength == 0 || pathLength >= MAX_PATH)
            return false;

        wchar_t logPath[MAX_PATH]{};

        if (swprintf(
            logPath,
            MAX_PATH,
            L"%sgusto-rof2-diagnostic.log",
            tempPath) < 0)
            return false;

        FILE* file = nullptr;

        if (_wfopen_s(&file, logPath, L"w") != 0 || !file)
            return false;

        std::fprintf(file, "GUSTO RoF2 diagnostic\n");
        std::fprintf(file, "=====================\n\n");

        std::fprintf(file, "Validation status: %s\n",
            report.IsValid() ? "VALID" : "FAILED");

        std::fprintf(file, "Module base: 0x%08lX\n",
            static_cast<unsigned long>(report.moduleBase));

        std::fprintf(file, "Image size: 0x%08lX\n",
            static_cast<unsigned long>(report.imageSize));

        std::fprintf(file, "Client date: %s\n", report.actualDate);
        std::fprintf(file, "Client time: %s\n", report.actualTime);
        std::fprintf(file, "SHA256: %s\n\n", report.sha256);

        std::fprintf(file, "Player snapshot\n");
        std::fprintf(file, "---------------\n");
        std::fprintf(file, "Available: %s\n",
            playerAvailable ? "yes" : "no");

        std::fprintf(file, "Name valid: %s\n",
            player.nameValid ? "yes" : "no");

        std::fprintf(file, "Name: %s\n", player.name);

        std::fprintf(file, "Displayed name valid: %s\n",
            player.displayedNameValid ? "yes" : "no");

        std::fprintf(file, "Displayed name: %s\n",
            player.displayedName);

        std::fprintf(file, "Level valid: %s\n",
            player.levelValid ? "yes" : "no");

        std::fprintf(file, "Level: %u\n",
            static_cast<unsigned>(player.level));

        std::fprintf(file, "Position valid: %s\n",
            player.positionValid ? "yes" : "no");

        std::fprintf(file, "Position Y/X/Z: %.3f / %.3f / %.3f\n",
            player.y, player.x, player.z);

        std::fprintf(file, "Heading valid: %s\n",
            player.headingValid ? "yes" : "no");

        std::fprintf(file, "Heading: %.3f\n\n", player.heading);

        std::fprintf(file, "Zone snapshot\n");
        std::fprintf(file, "-------------\n");
        std::fprintf(file, "Available: %s\n",
            zoneAvailable ? "yes" : "no");

        std::fprintf(file, "Character name valid: %s\n",
            zone.characterNameValid ? "yes" : "no");

        std::fprintf(file, "Character name: %s\n",
            zone.characterName);

        std::fprintf(file, "Short name valid: %s\n",
            zone.shortNameValid ? "yes" : "no");

        std::fprintf(file, "Short name: %s\n", zone.shortName);

        std::fprintf(file, "Long name valid: %s\n",
            zone.longNameValid ? "yes" : "no");

        std::fprintf(file, "Long name: %s\n", zone.longName);

        std::fprintf(file, "Zone ID valid: %s\n",
            zone.zoneIdValid ? "yes" : "no");

        std::fprintf(file, "Zone ID: %lu\n",
            static_cast<unsigned long>(zone.zoneId));

        std::fclose(file);
        return true;
    }
}

extern "C" __declspec(dllexport)
int GustoDumpClientState()
{
    using namespace gusto::client::rof2;

    if (GetInitializationStatus() != InitializationStatus::Ready)
        return DiagnosticValidationFailed;

    ValidationReport report{};

    if (!GetValidationReport(report))
        return DiagnosticValidationFailed;

    const ClientState* state = GetClientState();

    if (!state)
        return DiagnosticValidationFailed;

    PlayerSnapshot player{};
    ZoneSnapshot zone{};

    const bool playerAvailable =
        state->GetPlayerSnapshot(player);

    const bool zoneAvailable =
        state->GetZoneSnapshot(zone);

    if (!WriteDiagnosticLog(
        report,
        player,
        zone,
        playerAvailable,
        zoneAvailable))
        return DiagnosticLogFailure;

    if (!playerAvailable)
        return DiagnosticPlayerUnavailable;

    if (!zoneAvailable)
        return DiagnosticZoneUnavailable;

    return DiagnosticSuccess;
}
