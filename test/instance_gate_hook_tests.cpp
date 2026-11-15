#include "gusto/client/rof2/bank.hpp"
#include "gusto/client/rof2/tag_hooks.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

// Build as a 32-bit executable at image base 0x10000000. Run:
// gusto_instance_gate_hook_tests.exe [path-to-May-10-2013-eqgame.exe]
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
        Check(module != nullptr, "cannot map client image");
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

std::string sent;
void *sentGame = nullptr, *sentPlayer = nullptr;
void __fastcall CaptureCommand(void *game, void *, void *player, const char *command) {
    sentGame = game;
    sentPlayer = player;
    sent = command;
    // InterpretCmd reenters TickTags in the live plugin. It must not resend.
    TickInstanceGate();
}
void GateTest(const Image& image, const ValidatedClient& token) {
    ValidatedClient invalid;
    Check(!InstallInstanceGateHooks(image.module, invalid), "invalid token installed gate hook");
    Check(InstallInstanceGateHooks(image.module, token), "instance chat hook installation failed");
    Check(InstallInstanceGateHooks(image.module, token), "gate installation is not idempotent");
    Check(image.At(0x51f1a0)[0] == 0xe9, "chat entry not hooked");
    std::vector<BYTE> game(0x600), player(0x2000);
    int state = 5;
    std::memcpy(game.data() + 0x5c8, &state, sizeof(state));
    void *gamePointer = game.data(), *playerPointer = player.data();
    Write(image.At(0xe67ccc, 4), &gamePointer, 4);
    Write(image.At(0xdd2630, 4), &playerPointer, 4);
    BYTE jump[5] = {0xe9};
    auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&CaptureCommand) -
        reinterpret_cast<std::uintptr_t>(image.At(0x51fce0)) - 5);
    std::memcpy(jump + 1, &delta, 4);
    Write(image.At(0x51fce0, 5), jump, 5);
    using Chat = void(__thiscall *)(void *, const char *, int, bool, bool);
    auto chat = reinterpret_cast<Chat>(image.At(0x51f1a0));
    const char *challenge = "Verification code: [GUSTO_INSTANCE_GATE:8d6af98b0272a8ebaf]";
    chat(gamePointer, challenge, 0, true, true);
    Check(sent.empty(), "sent reentrantly from incoming chat");
    TickInstanceGate();
    Check(sent == "/say #gusto verify 8d6af98b0272a8ebaf", "challenge did not reach native interpreter");
    Check(sentGame == gamePointer && sentPlayer == playerPointer, "wrong native command context");
    sent.clear();
    chat(gamePointer, challenge, 0, true, true);
    TickInstanceGate();
    Check(sent.empty(), "duplicate challenge sent twice");
    // A new challenge must survive temporary loss of the local player.
    playerPointer = nullptr;
    Write(image.At(0xdd2630, 4), &playerPointer, 4);
    chat(gamePointer, "[GUSTO_INSTANCE_GATE:abcdef1234]", 0, true, true);
    TickInstanceGate();
    Check(sent.empty(), "sent without local player");
    playerPointer = player.data();
    Write(image.At(0xdd2630, 4), &playerPointer, 4);
    TickInstanceGate();
    Check(sent == "/say #gusto verify abcdef1234", "deferred reply was lost");
    sent.clear();
    chat(gamePointer, "GUSTO_INSTANCE_GATE:fedcba4321", 0, true, true);
    TickInstanceGate();
    Check(sent == "/say #gusto verify fedcba4321", "bare marker did not send");
    sent.clear();
    const char *liveChallenge = "GUSTO.asi is required for this guild or PvP instance. If installed, it will verify automatically; after the confirmation, click the book again. Verification code: [\x12" "0FFFFF01CC2000000000000000000000000000000000000000000000GUSTO_INSTANCE_GATE:8d6af9918d70ba3bd8\x12]";
    chat(gamePointer, liveChallenge, 0, true, true);
    TickInstanceGate();
    Check(sent == "/say #gusto verify 8d6af9918d70ba3bd8", "live saylink challenge did not send");
    std::puts("PASS real RoF2 chat entry -> deferred native command, context, reentry, deduplication and retry");
}
}

int main(int argc, char** argv)
{
    try {
        Check(argc <= 2, "usage: gusto_instance_gate_hook_tests.exe [eqgame.exe]");
        Image image;
        image.Open(argc == 2 ? argv[1] : "eqgame.exe");
        ValidationReport report;
        ValidatedClient token;
        if (!ValidateLoadedClient(image.module, report, token)) {
            std::fprintf(stderr, "validation status: %u\n", static_cast<unsigned>(report.status));
            Check(false, "unsupported client image");
        }
        GateTest(image, token);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s (Win32 error %lu)\n", error.what(), GetLastError());
        return 1;
    }
}
