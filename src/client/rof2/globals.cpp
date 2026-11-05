#include "gusto/client/rof2/globals.hpp"

#include <algorithm>
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

            const auto end =
                start + static_cast<std::uintptr_t>(length);

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

                const auto regionEnd =
                    regionBase + static_cast<std::uintptr_t>(info.RegionSize);

                if (regionEnd <= current)
                    return false;

                current = (std::min)(regionEnd, end);
            }

            return true;
        }

        bool IsValidatedModule(
            HMODULE module,
            const ValidatedClient& validation)
        {
            return validation.IsValidFor(module);
        }
    }

    void* ResolveObject(
        HMODULE module,
        const ValidatedClient& validation,
        const addresses::Global& global,
        std::size_t objectSize)
    {
        if (!IsValidatedModule(module, validation) ||
            global.storage != addresses::Storage::DirectObject ||
            objectSize == 0)
            return nullptr;

        const auto address = ResolvePreferredVA(
            module,
            global.preferredVA,
            objectSize);

        return address
            ? reinterpret_cast<void*>(address)
            : nullptr;
    }

    void* ResolvePointer(
        HMODULE module,
        const ValidatedClient& validation,
        const addresses::Global& global,
        std::size_t targetSize)
    {
        static_assert(sizeof(void*) == 4,
            "GUSTO requires a 32-bit target.");

        if (!IsValidatedModule(module, validation) ||
            global.storage != addresses::Storage::PointerStorage ||
            targetSize == 0)
            return nullptr;

        const auto storageAddress = ResolvePreferredVA(
            module,
            global.preferredVA,
            sizeof(void*));

        if (!storageAddress)
            return nullptr;

        const auto* storage =
            reinterpret_cast<const void*>(storageAddress);

        if (!IsReadableRange(storage, sizeof(void*)))
            return nullptr;

        void* target = nullptr;

        // Read the pointer without assuming an aliasing type.
        std::memcpy(&target, storage, sizeof(target));

        if (!target || !IsReadableRange(target, targetSize))
            return nullptr;

        return target;
    }
}
