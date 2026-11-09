#include <windows.h>

#include <cstdio>
#include <cstdint>

#include "gusto/client/rof2/addresses.hpp"
#include "gusto/client/rof2/client_state.hpp"
#include "gusto/client/rof2/globals.hpp"
#include "gusto/client/rof2/initialization.hpp"
#include "gusto/client/rof2/tag_hooks.hpp"
#include "gusto/client/rof2/hook_utils.hpp"

using namespace gusto::client::rof2;

namespace
{
    int failures = 0;

    void Check(bool condition, const char* description)
    {
        if (condition)
            std::printf("[PASS] %s\n", description);
        else
        {
            std::printf("[FAIL] %s\n", description);
            ++failures;
        }
    }
}

int main()
{
    std::printf("GUSTO RoF2 safety tests\n\n");

    ValidationReport report{};
    ValidatedClient token{};

    Check(
        !ValidateLoadedClient(nullptr, report, token),
        "Reject null client module");

    Check(
        report.status == ValidationStatus::NullModule,
        "Report NullModule status");

    Check(
        !token.IsValidFor(GetModuleHandleW(nullptr)),
        "Default token cannot authorize a module");

    Check(
        ResolvePreferredVA(nullptr, 0x00400000, 1) == 0,
        "Reject null module in preferred-VA resolver");

    Check(
        ResolvePreferredVA(
            GetModuleHandleW(nullptr),
            0x003FFFFF,
            1) == 0,
        "Reject preferred VA below image base");

    const HMODULE self = GetModuleHandleW(nullptr);
    Check(!InstallTagHooks(nullptr, token), "Reject tagging hooks on null module");
    Check(!InstallTagHooks(self, token), "Reject tagging hooks without validation token");
    BYTE companionPrologue[] = {0x83, 0xec, 0x20, 0x80, 0x7c, 0x24, 0x24, 0};
    void* companionTrampoline = nullptr;
    Check(!InstallModuleDetour(self, token, self, 0xae370, companionPrologue,
        sizeof(companionPrologue), reinterpret_cast<void*>(&TickTags), &companionTrampoline),
        "Reject companion-module detour without client validation");
    Check(companionTrampoline == nullptr, "Failed companion detour leaves trampoline unpublished");
    TickTags();
    Check(GetActorTagText(nullptr).empty(), "Ignore tags before client validation");

    const auto selfDosHeader = ResolvePreferredVA(
        self,
        PreferredImageBase,
        sizeof(IMAGE_DOS_HEADER));

    Check(
        selfDosHeader == reinterpret_cast<std::uintptr_t>(self),
        "Resolve PE image base to DOS header");

    Check(
        ResolvePreferredVA(
            self,
            PreferredImageBase + 0xFFFFFFFFu,
            1) == 0,
        "Reject address outside test executable image");

    // Editing diagnostic data must not create an authorization token.
    report.status = ValidationStatus::Valid;
    report.moduleBase = reinterpret_cast<std::uintptr_t>(self);

    Check(
        report.IsValid(),
        "Diagnostic report can be modified independently");

    Check(
        !token.IsValidFor(self),
        "Modified diagnostic report cannot authorize a module");

    Check(
        ResolveObject(
            self,
            token,
            addresses::EverQuestInfo,
            addresses::EverQuestInfoReferenceSize) == nullptr,
        "Reject object resolution without validation token");

    Check(
        ResolvePointer(
            self,
            token,
            addresses::LocalPlayer,
            addresses::PlayerClientReferenceSize) == nullptr,
        "Reject pointer resolution without validation token");

    ClientState invalidState(self, token);

    Check(
        !invalidState.IsValid(),
        "Reject ClientState without validation token");

    Check(
        invalidState.GetEverQuestInfo() == nullptr,
        "Do not resolve EverQuestInfo from invalid state");

    Check(
        invalidState.GetLocalPlayer() == nullptr,
        "Do not resolve local player from invalid state");

    PlayerSnapshot playerSnapshot{};
    playerSnapshot.available = true;
    playerSnapshot.level = 99;

    Check(
        !invalidState.GetPlayerSnapshot(playerSnapshot),
        "Reject player snapshot without validation token");

    Check(
        !playerSnapshot.available && playerSnapshot.level == 0,
        "Clear player snapshot when validation fails");

    ZoneSnapshot zoneSnapshot{};
    zoneSnapshot.available = true;
    zoneSnapshot.zoneId = 999;

    Check(
        !invalidState.GetZoneSnapshot(zoneSnapshot),
        "Reject zone snapshot without validation token");

    Check(
        !zoneSnapshot.available && zoneSnapshot.zoneId == 0,
        "Clear zone snapshot when validation fails");

    // Initialization must reject the safety-test executable.
    Check(
        InitializeClient() == InitializationStatus::ValidationFailed,
        "Reject initialization against an unsupported executable");

    Check(
        GetInitializationStatus() == InitializationStatus::ValidationFailed,
        "Retain initialization failure status");

    Check(
        InitializeClient() == InitializationStatus::ValidationFailed,
        "Return cached initialization failure");

    Check(
        GetClientState() == nullptr,
        "Do not expose ClientState after failed initialization");

    ValidationReport initializationReport{};

    Check(
        !GetValidationReport(initializationReport),
        "Do not expose a successful validation report after failure");

    Check(
        GustoInitializeClient() == 0,
        "Exported initialization API reports validation failure");

    std::printf("\n%d failure(s)\n", failures);

    return failures == 0 ? 0 : 1;
}
