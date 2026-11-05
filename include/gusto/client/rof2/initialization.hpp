#pragma once

#include "gusto/client/rof2/client_state.hpp"
#include "gusto/client/rof2/compatibility.hpp"

namespace gusto::client::rof2
{
    enum class InitializationStatus
    {
        NotStarted,
        Initializing,
        Ready,
        ValidationFailed
    };

    InitializationStatus InitializeClient();
    InitializationStatus GetInitializationStatus();
    const ClientState* GetClientState();
    bool GetValidationReport(ValidationReport& report);
}

extern "C" __declspec(dllexport)
int GustoInitializeClient();
