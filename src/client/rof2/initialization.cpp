#include "gusto/client/rof2/initialization.hpp"

#include "gusto/client/rof2/compatibility.hpp"
#include "gusto/client/rof2/bank.hpp"
#include "gusto/client/rof2/mercenary_ui.hpp"
#include "gusto/client/rof2/interpret_cmd_hook.hpp"
#include "gusto/client/rof2/slow_turn_keys.hpp"

#include "gusto/client/rof2/nameplate_hooks.hpp"
#include "gusto/client/rof2/tag_hooks.hpp"
#include "gusto/client/rof2/hud_overlay.hpp"

#include <windows.h>

namespace gusto::client::rof2
{
    namespace
    {
        SRWLOCK g_lock = SRWLOCK_INIT;

        InitializationStatus g_status =
            InitializationStatus::NotStarted;

        ValidationReport g_report{};
        ValidatedClient g_validation{};
        ClientState* g_clientState = nullptr;
    }

    InitializationStatus InitializeClient()
    {
        AcquireSRWLockExclusive(&g_lock);

        if (g_status == InitializationStatus::Ready ||
            g_status == InitializationStatus::ValidationFailed ||
            g_status == InitializationStatus::HookInstallFailed)
        {
            const InitializationStatus status = g_status;
            ReleaseSRWLockExclusive(&g_lock);
            return status;
        }

        g_status = InitializationStatus::Initializing;

        const HMODULE module = GetModuleHandleW(nullptr);

        if (!ValidateLoadedClient(module, g_report, g_validation))
        {
            g_status = InitializationStatus::ValidationFailed;
            ReleaseSRWLockExclusive(&g_lock);
            return InitializationStatus::ValidationFailed;
        }

        InstallBankExpansion(module, g_validation);

        InstallMercenaryUiExpansion(module, g_validation);

        InstallTagHooks(module, g_validation);
        InstallHudHooks(module, g_validation);

        InstallInterpretCmdHook(module, g_validation);

        install_slow_turn_keys(module, g_validation);

        InstallNameplateHooks(module, g_validation);

        static ClientState clientState(module, g_validation);
        g_clientState = &clientState;

        g_status = InitializationStatus::Ready;

        ReleaseSRWLockExclusive(&g_lock);
        return InitializationStatus::Ready;
    }

    InitializationStatus GetInitializationStatus()
    {
        AcquireSRWLockShared(&g_lock);
        const InitializationStatus status = g_status;
        ReleaseSRWLockShared(&g_lock);
        return status;
    }

    const ClientState* GetClientState()
    {
        AcquireSRWLockShared(&g_lock);

        const ClientState* clientState =
            g_status == InitializationStatus::Ready
                ? g_clientState
                : nullptr;

        ReleaseSRWLockShared(&g_lock);
        return clientState;
    }

    bool GetValidationReport(ValidationReport& report)
    {
        AcquireSRWLockShared(&g_lock);

        const bool ready =
            g_status == InitializationStatus::Ready;

        if (ready)
            report = g_report;

        ReleaseSRWLockShared(&g_lock);
        return ready;
    }
}

extern "C" __declspec(dllexport)
int GustoInitializeClient()
{
    using gusto::client::rof2::InitializeClient;
    using gusto::client::rof2::InitializationStatus;

    switch (InitializeClient())
    {
        case InitializationStatus::Ready:
            return 1;

        case InitializationStatus::ValidationFailed:
        case InitializationStatus::HookInstallFailed:
            return 0;

        default:
            return -1;
    }
}
