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
void *sentGame = nullptr, *usedBook = nullptr;
int bookUses = 0;
float usedHit[3]{};
void Jump(const Image& image, std::uintptr_t va, const void *target) {
    BYTE jump[5] = {0xe9};
    auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target) -
        reinterpret_cast<std::uintptr_t>(image.At(va)) - 5);
    std::memcpy(jump + 1, &delta, 4);
    Write(image.At(va, 5), jump, 5);
}
void __fastcall CaptureBook(void *book, void *, unsigned user, int key, int skill, const float *hit) {
    Check(user == 1234 && key == -1 && skill == 0, "book replay arguments changed");
    usedBook = book; ++bookUses;
    if (hit) std::memcpy(usedHit, hit, sizeof(usedHit));
}
void __fastcall CaptureCommand(void *game, void *, int channel, const char *target, const char *command, int language) {
    Check(channel == 8 && !target && language == 0, "wrong quiet command transport");
    sentGame = game;
    sent = command;
    TickInstanceGate();
}
void GateTest(const Image& image, const ValidatedClient& token) {
    // Preserve UseSwitch's validated prologue, but capture its continuation.
    Jump(image, 0x511d75, reinterpret_cast<const void *>(&CaptureBook));
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
    Jump(image, 0x51dcd0, reinterpret_cast<const void *>(&CaptureCommand));
    unsigned playerId = 1234;
    std::memcpy(player.data() + 0x148, &playerId, 4);
    std::vector<BYTE> book(0x100), otherBook(0x100);
    void *vtable = book.data();
    std::memcpy(book.data(), &vtable, 4); book[5] = 7;
    std::memcpy(otherBook.data(), &vtable, 4); otherBook[5] = 8;
    float hit[3] = {1, 2, 3};
    using Use = void(__thiscall *)(void *, unsigned, int, int, const float *);
    auto use = reinterpret_cast<Use>(image.At(0x511d70));
    using Chat = void(__thiscall *)(void *, const char *, int, bool, bool);
    auto chat = reinterpret_cast<Chat>(image.At(0x51f1a0));
    const char *challenge = "Verification code: [GUSTO_INSTANCE_GATE:8d6af98b0272a8ebaf]";
    chat(gamePointer, challenge, 0, true, true);
    Check(sent.empty(), "sent reentrantly from incoming chat");
    TickInstanceGate();
    Check(sent == "#gusto verify 8d6af98b0272a8ebaf", "challenge did not reach native interpreter");
    Check(sentGame == gamePointer, "wrong native command context");
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
    Check(sent == "#gusto verify abcdef1234", "deferred reply was lost");
    sent.clear();
    chat(gamePointer, "GUSTO_INSTANCE_GATE:fedcba4321", 0, true, true);
    TickInstanceGate();
    Check(sent == "#gusto verify fedcba4321", "bare marker did not send");
    sent.clear();
    const char *liveChallenge = "GUSTO.asi is required for this guild or PvP instance. If installed, it will verify automatically; after the confirmation, click the book again. Verification code: [\x12" "0FFFFF01CC2000000000000000000000000000000000000000000000GUSTO_INSTANCE_GATE:8d6af9918d70ba3bd8\x12]";
    chat(gamePointer, liveChallenge, 0, true, true);
    TickInstanceGate();
    Check(sent == "#gusto verify 8d6af9918d70ba3bd8", "live saylink challenge did not send");
    // One player click, then server challenge, then confirmation: one replay.
    use(book.data(), 1234, -1, 0, hit);
    Check(bookUses == 1, "original book click was swallowed");
    chat(gamePointer, "[GUSTO_INSTANCE_GATE:998877]", 0, true, true);
    TickInstanceGate();
    Check(sent == "#gusto verify 998877", "one-click verification not sent");
    Check(bookUses == 1, "book retried before verification confirmation");
    // A different subsequent click cannot redirect the original request.
    use(otherBook.data(), 1234, -1, 0, nullptr);
    hit[0] = 99;
    chat(gamePointer, "GUSTO.asi verified. Click the instance book again to enter.", 0, true, true);
    Check(bookUses == 2, "book retried reentrantly from chat");
    TickInstanceGate();
    Check(bookUses == 3 && usedBook == book.data(), "verified original book not retried once");
    Check(usedHit[0] == 1 && usedHit[1] == 2 && usedHit[2] == 3, "hit position was not copied");
    chat(gamePointer, "GUSTO.asi verified. Click the instance book again to enter.", 0, true, true);
    TickInstanceGate();
    Check(bookUses == 3, "duplicate confirmation replayed book twice");
    std::puts("PASS link challenges, quiet transport, context/reentry/retry, hidden confirmation and one-click book replay");
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
