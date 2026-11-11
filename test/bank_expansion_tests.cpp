#include "gusto/client/rof2/bank.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <array>

// Build as a 32-bit executable at image base 0x10000000. Run:
// gusto_bank_tests.exe [path-to-unmodified-May-10-2013-eqgame.exe]
// The default path is eqgame.exe in the current directory. No game is launched.
namespace {
using namespace gusto::client::rof2;
static_assert(sizeof(void*) == 4, "Native client tests require x86");

void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct Image {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    HMODULE module = nullptr;
    ~Image() {
        if (module) UnmapViewOfFile(module);
        if (mapping) CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
    BYTE* At(std::uintptr_t va, std::size_t size = 1) const {
        auto address = ResolvePreferredVA(module, va, size);
        Check(address != 0, "client address outside image");
        return reinterpret_cast<BYTE*>(address);
    }
    void Open(const char* path) {
        file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "cannot open eqgame.exe");
        mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY | SEC_IMAGE, 0, 0, nullptr);
        Check(mapping != nullptr, "SEC_IMAGE mapping failed");
        module = static_cast<HMODULE>(MapViewOfFileEx(mapping, FILE_MAP_READ, 0, 0, 0,
            reinterpret_cast<void*>(PreferredImageBase)));
        Check(reinterpret_cast<std::uintptr_t>(module) == PreferredImageBase,
            "cannot map at 0x400000; link test at 0x10000000");
    }
};

void Write(void* destination, const void* source, std::size_t size)
{
    DWORD old = 0, ignored = 0;
    Check(VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &old) != 0,
        "test memory protection failed");
    std::memcpy(destination, source, size);
    const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), destination, size);
    const BOOL restored = VirtualProtect(destination, size, old, &ignored);
    Check(flushed && restored, "test write publication failed");
}

void CheckSites(const Image& image, const std::vector<CodePatch>& patches, bool installed)
{
    for (const auto& patch : patches) {
        const auto& bytes = installed ? patch.replacement : patch.expected;
        if (std::memcmp(image.At(patch.preferredVa, bytes.size()), bytes.data(), bytes.size())) {
            std::fprintf(stderr, "mismatch at %08lx\n", static_cast<unsigned long>(patch.preferredVa));
            Check(false, "unexpected patch bytes");
        }
    }
}

void PatchTests(Image& image, const ValidatedClient& token)
{
    auto plan = BuildBankExpansionPatches(image.module, nullptr);
    CheckSites(image, plan, false);
    ValidatedClient invalid;
    Check(!ApplyCodePatches(image.module, invalid, plan), "invalid token patched code");
    Check(!InstallBankExpansion(image.module, invalid), "invalid token installed hooks");
    CheckSites(image, plan, false);

    // Corrupt the LAST expected site in the request, not the mapped image.
    // Earlier valid sites must remain untouched when the transaction rejects it.
    auto mismatch = plan;
    mismatch.back().expected.back() ^= 1;
    Check(!ApplyCodePatches(image.module, token, mismatch), "mismatched transaction succeeded");
    CheckSites(image, plan, false);

    for (std::uintptr_t va : {0x00d1fc90u, 0x00dd261cu}) {
        auto* global = image.At(va, 4);
        DWORD saved = 0, busy = 1;
        std::memcpy(&saved, global, 4);
        Check(saved == 0, "unexpected initialized client global");
        Write(global, &busy, 4);
        const bool installed = InstallBankExpansion(image.module, token);
        Write(global, &saved, 4);
        Check(!installed, "late installation succeeded");
        CheckSites(image, plan, false);
    }
    Check(InstallBankExpansion(image.module, token), "bank installation failed");
    auto* hover = image.At(0x6288bd, 9);
    Check(hover[0] == 0xe9, "hover is not a JMP");
    std::int32_t displacement = 0;
    std::memcpy(&displacement, hover + 1, 4);
    auto* thunk = hover + 5 + displacement;
    plan = BuildBankExpansionPatches(image.module, thunk);
    CheckSites(image, plan, true);
    Check(InstallBankExpansion(image.module, token), "installation is not idempotent");
    Check(!InstallBankExpansion(image.module, invalid), "active hooks accepted invalid token");
    CheckSites(image, plan, true);
    std::puts("PASS patch validation, atomic rejection, startup globals, installation and all JMP bytes");
}

DWORD Word(const BYTE* address)
{
    DWORD value;
    std::memcpy(&value, address, 4);
    return value;
}

void ContainerTests(const Image& image)
{
    // thiscall: ECX=this, three stack arguments, callee pops 12 bytes.
    using Constructor = void* (__thiscall*)(void*, int, int, int);
    auto construct = reinterpret_cast<Constructor>(image.At(0x7e57b0));
    for (auto site : {0x7cce99u, 0x7ccea9u}) {
        const unsigned count = image.At(site, 2)[1];
        const unsigned kind = site == 0x7cce99 ? 1 : 2;
        Check(count == (kind == 1 ? BankSlots : SharedBankSlots), "wrong PcBase capacity");
        std::array<BYTE, 0x58> storage;
        storage.fill(0xa5);
        BYTE* object = storage.data() + 16;
        Check(construct(object, count, kind, 0) == object, "constructor return value");
        Check(Word(object) == count && Word(object + 4) == kind, "container size or kind");
        Check(Word(object + 8) == 0 && Word(object + 12) == 0 && Word(object + 16) == 0,
            "container array not empty");
        Check(object[0x14] == 0 && Word(object + 0x16) == 0xffffffff && object[0x1a] == 0,
            "container bookkeeping flags");
        Check(Word(object + 0x20) == count && Word(object + 0x28) == 0 && Word(object + 0x2c) == 0,
            "container bit bookkeeping");
        for (unsigned i = 0; i < 16; ++i)
            Check(storage[i] == 0xa5 && storage[0x48 + i] == 0xa5, "container canary overwritten");
    }
    std::puts("PASS native ItemContainer construction using patched PcBase capacities (32/8)");
}

void WindowZeroTests(const Image& image)
{
    // Copy only the self-contained block [627cda,627d05). Its short
    // branches remain relative to the copied bytes. No GUI constructor runs.
    std::array<BYTE, BankWindowSize + 32> storage;
    storage.fill(0xa5);
    BYTE* object = storage.data() + 16;
    std::vector<BYTE> code{0x53, 0x56, 0x57, 0x55, // save nonvolatile registers
        0x83, 0xec, 0x20,                       // scratch for [esp+13]
        0x8b, 0x74, 0x24, 0x34,                 // ESI = argument
        0x33, 0xdb, 0x33, 0xc9};                // EBX=ECX=0
    const auto* block = image.At(0x627cda, 0x627d05 - 0x627cda);
    code.insert(code.end(), block, block + 0x627d05 - 0x627cda);
    const BYTE tail[]{0x83,0xc4,0x20,0x5d,0x5f,0x5e,0x5b,0xc3};
    code.insert(code.end(), std::begin(tail), std::end(tail));
    void* wrapper = VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Check(wrapper != nullptr, "wrapper allocation failed");
    std::memcpy(wrapper, code.data(), code.size());
    DWORD old;
    if (!VirtualProtect(wrapper, code.size(), PAGE_EXECUTE_READ, &old) ||
        !FlushInstructionCache(GetCurrentProcess(), wrapper, code.size())) {
        VirtualFree(wrapper, 0, MEM_RELEASE);
        Check(false, "wrapper publication failed");
    }
    reinterpret_cast<void (__cdecl*)(void*)>(wrapper)(object);
    VirtualFree(wrapper, 0, MEM_RELEASE);
    Check(Word(object + 0x2c4) == BankSlots, "native BankSize initialization");
    for (unsigned i = 0; i < storage.size(); ++i) {
        const bool sizeByte = i >= 16 + 0x2c4 && i < 16 + 0x2c8;
        const bool cleared = i >= 16 + BankWindowSlotsOffset && i < 16 + SharedBankWindowSlotsOffset;
        if (!sizeByte) Check(storage[i] == (cleared ? 0 : 0xa5), "zero block damaged fields or canaries");
    }
    Check(Word(object + 0x2a8) == 0xa5a5a5a5, "money button moved or cleared");
    std::puts("PASS native bank zero block, 32 appended slots, original fields and canaries");
}

void DecoderTests(const Image& image)
{
    using Decoder = void* (__cdecl*)(void*, int);
    auto decode = reinterpret_cast<Decoder>(image.At(0x694180));
    for (unsigned kind : {1u, 2u}) {
        const unsigned base = kind == 1 ? 2032 : 2532;
        const unsigned count = kind == 1 ? BankSlots : SharedBankSlots;
        // Every bag and all ten subslots, including the old and new limits.
        for (unsigned offset = 0; offset < count * 10; ++offset) {
            std::array<BYTE, 32> result;
            result.fill(0xa5);
            auto* output = result.data() + 8;
            Check(decode(output, base + offset) == output, "decoder return value");
            Check(Word(output) == kind, "decoder container kind");
            std::int16_t indices[3];
            std::memcpy(indices, output + 4, 6);
            Check(indices[0] == offset / 10 && indices[1] == offset % 10 && indices[2] == -1,
                "decoder bag/subslot boundary");
            for (unsigned i = 0; i < result.size(); ++i)
                if (i < 8 || i >= 18) Check(result[i] == 0xa5, "decoder canary overwritten");
        }
        std::array<BYTE, 12> result{};
        decode(result.data(), base + count * 10);
        Check(Word(result.data()) == 0xffffffff, "decoder accepted one-past-last bag");
        decode(result.data(), base - 1);
        std::int16_t index;
        std::memcpy(&index, result.data() + 4, 2);
        Check(Word(result.data()) == kind && index == 31, "direct-slot upper boundary changed");
    }
    std::puts("PASS native legacy decoder: all 320/80 bag subslots and adjacent boundaries");
}
} // namespace

int main(int argc, char** argv)
{
    try {
        Check(argc <= 2, "usage: gusto_bank_tests.exe [eqgame.exe]");
        Image image;
        image.Open(argc == 2 ? argv[1] : "eqgame.exe");
        ValidationReport report;
        ValidatedClient token;
        if (!ValidateLoadedClient(image.module, report, token)) {
            std::fprintf(stderr, "validation status: %u\n", static_cast<unsigned>(report.status));
            Check(false, "unsupported client image");
        }
        PatchTests(image, token);
        ContainerTests(image);
        WindowZeroTests(image);
        DecoderTests(image);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s (Win32 error %lu)\n", error.what(), GetLastError());
        return 1;
    }
}
