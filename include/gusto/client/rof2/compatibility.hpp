#pragma once

#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace gusto::client::rof2
{
    inline constexpr std::uint32_t ClientDate = 20130510;
    inline constexpr char ExpectedVersionDate[] = "May 10 2013";
    inline constexpr char ExpectedVersionTime[] = "23:30:08";

    inline constexpr std::uintptr_t PreferredImageBase = 0x00400000;
    inline constexpr std::uintptr_t VersionDateVA = 0x009DD250;
    inline constexpr std::uintptr_t VersionTimeVA = 0x009DD25C;
    inline constexpr std::uint32_t ExpectedImageSize = 0x012C3000;

    inline constexpr char ExpectedSha256[] =
        "2a8702ad9f722704f01355c0750be7d6f164a8b9c9128ba0cf286ea32b405b0e";

    enum class ValidationStatus
    {
        Valid,
        NullModule,
        InvalidDosHeader,
        InvalidNtHeader,
        UnsupportedMachine,
        UnsupportedOptionalHeader,
        UnexpectedImageBase,
        UnexpectedImageSize,
        AddressOutOfBounds,
        UnreadableMemory,
        VersionMismatch,
        ModulePathUnavailable,
        HashFailure,
        HashMismatch
    };

    struct ValidationReport
    {
        ValidationStatus status = ValidationStatus::NullModule;
        std::uintptr_t moduleBase = 0;
        std::uint32_t imageSize = 0;
        char actualDate[32]{};
        char actualTime[32]{};
        char sha256[65]{};

        bool IsValid() const
        {
            return status == ValidationStatus::Valid;
        }
    };

    class ValidatedClient
    {
    public:
        ValidatedClient() = default;

        ValidatedClient(const ValidatedClient&) = default;
        ValidatedClient& operator=(const ValidatedClient&) = default;

        bool IsValidFor(HMODULE module) const noexcept
        {
            return module != nullptr && m_module == module;
        }

    private:
        explicit ValidatedClient(HMODULE module) noexcept
            : m_module(module)
        {
        }

        HMODULE m_module = nullptr;

        friend bool ValidateLoadedClient(
            HMODULE module,
            ValidationReport& report,
            ValidatedClient& validatedClient);
    };

    // A successful validation produces a token tied to that exact module.
    // The diagnostic report alone does not authorize memory access.
    bool ValidateLoadedClient(
        HMODULE module,
        ValidationReport& report,
        ValidatedClient& validatedClient);

    std::uintptr_t ResolvePreferredVA(
        HMODULE module,
        std::uintptr_t preferredVA,
        std::size_t length);
}
