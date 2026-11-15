#include "gusto/client/rof2/bank.hpp"
#include "gusto/client/rof2/tag_hooks.hpp"
#include "gusto/client/rof2/server_select.hpp"
#include "gusto/client/rof2/server_select_model.hpp"
#include "gusto/client/rof2/ui_cleanup.hpp"
#include "gusto/client/rof2/ui_cleanup_policy.hpp"
#include "gusto/client/rof2/pvp_guard.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <array>
#include <map>

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
    void Open(const char* path, std::uintptr_t preferred = PreferredImageBase) {
        file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "cannot open eqgame.exe");
        mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY | SEC_IMAGE, 0, 0, nullptr);
        Check(mapping != nullptr, "SEC_IMAGE mapping failed");
        module = static_cast<HMODULE>(MapViewOfFileEx(mapping, FILE_MAP_READ, 0, 0, 0,
            reinterpret_cast<void*>(preferred)));
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
int pvpSent=0,pvpBlocked=0;
void __fastcall CapturePvpSend(void*,void*,int,const char*,const char*,int) { ++pvpSent; }
void __fastcall CapturePvpNotice(void*,void*,const char*,int,bool,bool) { ++pvpBlocked; }
void PvpGuardTest(const Image& image,const ValidatedClient& token) {
    ValidatedClient invalid;
    Check(!InstallPvpGuard(image.module,invalid),"invalid token installed PvP guard");
    // SendNewText's relocated exception-handler push remains in the trampoline.
    BYTE unwind[]={0x83,0xc4,0x08,0xe9,0,0,0,0};
    auto* thunk=static_cast<BYTE*>(VirtualAlloc(nullptr,sizeof(unwind),MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(thunk!=nullptr,"PvP test thunk allocation failed");
    auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&CapturePvpSend)-reinterpret_cast<std::uintptr_t>(thunk)-sizeof(unwind));
    std::memcpy(unwind+4,&delta,4);std::memcpy(thunk,unwind,sizeof(unwind));
    Jump(image,0x51dcd7,thunk);
    // SEC_IMAGE private mappings do not run the PE loader's base relocations.
    auto handler=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image.At(0x978448)));
    Write(image.At(0x51dcd3,4),&handler,4);
    Check(InstallPvpGuard(image.module,token),"PvP send guard installation failed");
    BYTE chatBytes[6];std::memcpy(chatBytes,image.At(0x51f1a0,6),6);
    Jump(image,0x51f1a0,reinterpret_cast<const void*>(&CapturePvpNotice));
    std::vector<BYTE> eq(0x600),player(0x2000);
    int state=5;std::memcpy(eq.data()+0x5c8,&state,4);player[0x349]=1;
    void* eqPointer=eq.data();void* playerPointer=player.data();
    Write(image.At(0xe67ccc,4),&eqPointer,4);Write(image.At(0xdd2630,4),&playerPointer,4);
    char zone[128]="Velketor's Labyrinth (PVP)";Write(image.At(0xddf8f0+0xc0,128),zone,128);
    using Send=void(__thiscall *)(void*,int,const char*,const char*,int);
    auto send=reinterpret_cast<Send>(image.At(0x51dcd0));
    send(eqPointer,8,nullptr,"#pvp",0);
    Check(pvpSent==0&&pvpBlocked==1,"off toggle reached server transport");
    Check(RejectPvpOffCommand(eqPointer,"/say #set pvp off"),"command-entry guard missed off command");
    send(eqPointer,8,nullptr,"#pvp on",0);
    Check(pvpSent==1,"explicit PvP enable was blocked");
    player[0x349]=0;send(eqPointer,8,nullptr,"#pvp",0);
    Check(pvpSent==2,"enabling toggle was blocked");
    player[0x349]=1;std::strcpy(zone,"Velketor's Labyrinth (GUILD)");Write(image.At(0xddf8f0+0xc0,128),zone,128);
    send(eqPointer,8,nullptr,"#pvp",0);Check(pvpSent==3,"ordinary-zone toggle blocked");
    player[0x2e1]=1;send(eqPointer,8,nullptr,"#pvp off",0);
    Check(pvpSent==3,"native PvP-area off command reached server");
    send(eqPointer,2,nullptr,"#pvp",0);Check(pvpSent==4,"non-command chat was blocked");
    Write(image.At(0x51f1a0,6),chatBytes,6);
    std::puts("PASS PvP client guard: marked zones and native PvP areas block off before transport; on/ordinary zones pass");
    VirtualFree(thunk,0,MEM_RELEASE);
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
std::string assignedName;
void* assignedNameField = nullptr;
void* __fastcall CaptureName(void* field, void*, const char* name) {
    assignedName = name; assignedNameField = field; return field;
}
int refreshCalls = 0;
void* refreshedWindow = nullptr;
void __fastcall CaptureRefresh(void* window, void*) { ++refreshCalls; refreshedWindow = window; }
int serverWireSize = 0;
int __stdcall CaptureServerParser(const std::uint8_t*, void* record) {
    std::memset(record, 0xa5, 0x48);
    const unsigned flags = 9;
    std::memcpy(static_cast<BYTE*>(record) + 0x30, &flags, 4);
    return serverWireSize;
}
void ServerSelectTest(const Image& client, const ValidatedClient& token, const char* path) {
    Image frontend;
    frontend.Open(path, 0);
    auto at = [&](unsigned rva, unsigned size = 1) {
        return reinterpret_cast<BYTE*>(ResolvePreferredVA(frontend.module, PreferredImageBase + rva, size));
    };
    // ReadServerData's 5-byte prologue saves EBP/ECX/EBX. Restore these
    // before calling a stand-in parser from its continuation.
    BYTE unwind[] = {0x5b,0x83,0xc4,0x04,0x5d,0xe9,0,0,0,0};
    auto* thunk = static_cast<BYTE*>(VirtualAlloc(nullptr,sizeof(unwind),MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(thunk != nullptr, "frontend parser test thunk allocation failed");
    auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&CaptureServerParser) -
        reinterpret_cast<std::uintptr_t>(thunk) - sizeof(unwind));
    std::memcpy(unwind+6,&delta,4); std::memcpy(thunk,unwind,sizeof(unwind));
    BYTE jump[] = {0xe9,0,0,0,0};
    delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thunk) - reinterpret_cast<std::uintptr_t>(at(0x1a605)) - 5);
    std::memcpy(jump+1,&delta,4); Write(at(0x1a605,5),jump,5);
    // CXStr's assignment prologue saves EBP. Unwind it for the mock setter.
    BYTE setterUnwind[] = {0x5d,0xe9,0,0,0,0};
    auto* setterThunk = static_cast<BYTE*>(VirtualAlloc(nullptr,sizeof(setterUnwind),MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(setterThunk != nullptr, "name setter thunk allocation failed");
    delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&CaptureName) - reinterpret_cast<std::uintptr_t>(setterThunk) - sizeof(setterUnwind));
    std::memcpy(setterUnwind+2,&delta,4); std::memcpy(setterThunk,setterUnwind,sizeof(setterUnwind));
    delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(setterThunk) - reinterpret_cast<std::uintptr_t>(at(0x475e6)) - 5);
    std::memcpy(jump+1,&delta,4); Write(at(0x475e6,5),jump,5);
    BYTE refreshUnwind[] = {0x83,0xc4,0x04,0x5d,0xe9,0,0,0,0};
    auto* refreshThunk = static_cast<BYTE*>(VirtualAlloc(nullptr,sizeof(refreshUnwind),MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(refreshThunk != nullptr, "refresh thunk allocation failed");
    delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&CaptureRefresh) - reinterpret_cast<std::uintptr_t>(refreshThunk) - sizeof(refreshUnwind));
    std::memcpy(refreshUnwind+5,&delta,4); std::memcpy(refreshThunk,refreshUnwind,sizeof(refreshUnwind));
    delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(refreshThunk) - reinterpret_cast<std::uintptr_t>(at(0x37835)) - 5);
    std::memcpy(jump+1,&delta,4); Write(at(0x37835,5),jump,5);
    ValidatedClient invalid;
    Check(!InstallServerSelectHook(client.module, invalid, frontend.module), "invalid client installed frontend hook");
    BYTE saved = *at(0x3678a), bad = saved ^ 1;
    Write(at(0x3678a),&bad,1);
    Check(!InstallServerSelectHook(client.module, token, frontend.module), "unknown frontend profile accepted");
    Check(*at(0x1a600) == 0x55, "rejected hook modified frontend");
    Write(at(0x3678a),&saved,1);
    Check(InstallServerSelectHook(client.module, token, frontend.module), "frontend server-list hook failed");
    Check(InstallServerSelectHook(client.module, token, frontend.module), "frontend hook not idempotent");
    using Parse = int(__stdcall *)(const std::uint8_t*,void*);
    auto parse = reinterpret_cast<Parse>(at(0x1a600));
    for (const char* name : {"[V a n g u a r d] Quarm", "[R] Vanguard", "[[R] ] Vanguard", "[[R]] Vanguard", " [ r ] Vanguard", "Other Server", "[R] Other Server"}) {
        std::string wire("world.example"); wire += '\0';
        wire.append(8,'\0'); wire += name; wire += '\0';
        wire += "US"; wire += '\0'; wire += "EN"; wire += '\0'; wire.append(8,'\0');
        serverWireSize = int(wire.size());
        std::vector<BYTE> record(0x48);
        assignedName.clear(); assignedNameField = nullptr;
        Check(parse(reinterpret_cast<const std::uint8_t*>(wire.data()),record.data()) == serverWireSize,
            "frontend parser return value changed");
        unsigned flags = 0; std::memcpy(&flags,record.data()+0x30,4);
        Check(flags == (IsVanguardServer(name) ? 17u : 9u), "frontend classification incorrect");
        if (VanguardDisplayName(name) != name) {
            Check(assignedName == "[] Vanguard" && assignedNameField == record.data()+4, "Vanguard display prefix not removed through CXStr");
        } else Check(assignedName.empty(), "unrelated display name changed");
        std::memset(record.data()+0x30,0xa5,4);
        for (auto byte : record) Check(byte == 0xa5, "frontend changed server identity/status");
    }
    // Model the native doubly linked server list, including CXStr payloads.
    std::array<std::array<BYTE,0x48>,4> servers{};
    std::array<std::array<BYTE,160>,4> names{};
    const char* texts[] = {"Other A", "[] Vanguard", "Other B", "Vanguard Test"};
    struct Node { void* data; Node* next; Node* previous; };
    std::array<Node,4> nodes{};
    for (int i=0;i<4;++i) {
        const unsigned length = unsigned(std::strlen(texts[i]));
        std::memcpy(names[i].data()+8,&length,4);
        std::memcpy(names[i].data()+0x14,texts[i],length+1);
        void* rep = names[i].data(); std::memcpy(servers[i].data()+4,&rep,4);
        nodes[i] = {servers[i].data(),i<3?&nodes[i+1]:nullptr,i?&nodes[i-1]:nullptr};
    }
    std::array<BYTE,0xf0> login{};
    void* head = &nodes[0]; void* tail = &nodes[3]; unsigned count = 4;
    std::memcpy(login.data()+0xd8,&head,4); std::memcpy(login.data()+0xdc,&tail,4);
    std::memcpy(login.data()+0xec,&count,4);
    void* loginPointer = login.data(); Write(at(0x15016c,4),&loginPointer,4);
    const auto recordsBefore = servers;
    using Refresh = void(__thiscall *)(void*);
    auto refresh = reinterpret_cast<Refresh>(at(0x37830));
    refresh(loginPointer);
    Check(refreshCalls == 1 && refreshedWindow == loginPointer, "native refresh not forwarded");
    Check(nodes[0].data == servers[1].data() && nodes[1].data == servers[3].data() &&
          nodes[2].data == servers[0].data() && nodes[3].data == servers[2].data(), "Vanguard not stably pinned to top");
    Check(servers == recordsBefore, "pinning changed server records");
    for (int i=0;i<4;++i) Check(nodes[i].next == (i<3?&nodes[i+1]:nullptr) &&
        nodes[i].previous == (i?&nodes[i-1]:nullptr), "pinning changed node links");
    auto pinned = nodes;
    refresh(loginPointer);
    for (int i=0;i<4;++i) Check(nodes[i].data == pinned[i].data, "repeated refresh reordered entries");
    for (int i=0;i<4;++i) nodes[i].data = servers[i].data();
    nodes[2].previous = nullptr;
    refresh(loginPointer);
    for (int i=0;i<4;++i) Check(nodes[i].data == servers[i].data(), "malformed list was modified");
    loginPointer = nullptr; Write(at(0x15016c,4),&loginPointer,4);
    const BYTE originalEntry[] = {0x55,0x8b,0xec,0x51,0x53};
    Write(at(0x1a600,5),originalEntry,5);
    Check(InstallServerSelectHook(client.module, token, frontend.module), "reloaded frontend was not rehooked");
    StartServerSelectHooks(client.module, token);
    StopServerSelectHooks();
    StartServerSelectHooks(client.module, token);
    StopServerSelectHooks();
    std::puts("PASS real eqmain detour, relocation, signature rejection, empty brackets, stable top pinning, record preservation and service restart");
    VirtualFree(refreshThunk,0,MEM_RELEASE);
    VirtualFree(setterThunk,0,MEM_RELEASE);
    VirtualFree(thunk,0,MEM_RELEASE);
}

std::map<std::pair<void*,std::string>,void*> uiChildren;
int stringsMade = 0, stringsFreed = 0, windowsHidden = 0, pagesRemoved = 0;
int __fastcall CaptureUiDraw(void*,void*) { return 77; }
void* __fastcall MakeUiString(void* field, void*, const char* name) {
    *static_cast<const char**>(field) = name; ++stringsMade; return field;
}
void __fastcall FreeUiString(void* field, void*, void* value) {
    Check(*static_cast<void**>(field) == value, "wrong native string released");
    *static_cast<void**>(field) = nullptr; ++stringsFreed;
}
void* __fastcall FindUiChild(void* parent, void*, const void* field) {
    const auto key = std::make_pair(parent,std::string(*static_cast<const char* const*>(field)));
    auto found = uiChildren.find(key); return found == uiChildren.end() ? nullptr : found->second;
}
int __fastcall HideUiWindow(void* window, void*, bool shown, bool top, bool layout) {
    Check(!shown && !top && layout, "wrong native hide arguments");
    static_cast<BYTE*>(window)[0x196] = 0; ++windowsHidden; return 0;
}
void __fastcall RemoveUiPage(void* tab, void*, void* page) {
    auto* bytes = static_cast<BYTE*>(tab);
    int count = 0; void** pages = nullptr;
    std::memcpy(&count,bytes+0x1e0,4); std::memcpy(&pages,bytes+0x1e4,4);
    int i = 0; while (i<count && pages[i]!=page) ++i;
    Check(i<count, "removed a page that is not a tab");
    for (;i+1<count;++i) pages[i] = pages[i+1];
    --count; std::memcpy(bytes+0x1e0,&count,4); ++pagesRemoved;
}
void UiCleanupTest(const Image& image, const ValidatedClient& token) {
    ValidatedClient invalid;
    Check(!InitializeUiCleanup(image.module,invalid), "invalid client initialized UI cleanup");
    Check(InitializeUiCleanup(image.module,token), "UI cleanup signature checks failed");
    Jump(image,0x805c20,reinterpret_cast<const void*>(&MakeUiString));
    Jump(image,0x805b20,reinterpret_cast<const void*>(&FreeUiString));
    Jump(image,0x868330,reinterpret_cast<const void*>(&FindUiChild));
    Jump(image,0x865290,reinterpret_cast<const void*>(&HideUiWindow));
    Jump(image,0x88d650,reinterpret_cast<const void*>(&RemoveUiPage));
    BYTE drawUnwind[]={0x83,0xc4,0x04,0xe9,0,0,0,0};
    auto* drawThunk=static_cast<BYTE*>(VirtualAlloc(nullptr,sizeof(drawUnwind),MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(drawThunk!=nullptr,"UI draw thunk allocation failed");
    auto drawDelta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&CaptureUiDraw)-reinterpret_cast<std::uintptr_t>(drawThunk)-sizeof(drawUnwind));
    std::memcpy(drawUnwind+4,&drawDelta,4);std::memcpy(drawThunk,drawUnwind,sizeof(drawUnwind));
    Jump(image,0x877608,drawThunk);
    using Window = std::array<BYTE,0x240>;
    std::array<Window,22> windows{};
    std::array<BYTE,0x164> vtable{};
    void* show = image.At(0x865290); std::memcpy(vtable.data()+0xd8,&show,4);
    for (auto& window : windows) {
        void* table = vtable.data(); std::memcpy(window.data(),&table,4); window[0x196] = 1;
    }
    auto root = windows[0].data(); auto tabs = windows[1].data();
    void* gamePointer = windows[21].data();
    std::vector<BYTE> eq(0x600); gamePointer = eq.data();
    int state = 0; std::memcpy(eq.data()+0x5c8,&state,4);
    Write(image.At(0xe67ccc,4),&gamePointer,4);
    void* inventory = root; void* eqMain = windows[16].data();
    Write(image.At(0xd1fc8c,4),&inventory,4); Write(image.At(0xf713c0,4),&eqMain,4);
    uiChildren[{root,"IW_Subwindows"}] = tabs;
    void* pages[] = {windows[2].data(),windows[3].data(),windows[4].data(),windows[5].data(),windows[6].data()};
    for (int i=0;i<3;++i) uiChildren[{root,InventoryRemovedPages[i]}] = pages[i+2];
    int count = 5; void* pageArray = pages;
    std::memcpy(tabs+0x1e0,&count,4); std::memcpy(tabs+0x1e4,&pageArray,4);
    for (int i=0;i<int(InventoryHiddenControls.size());++i) uiChildren[{root,InventoryHiddenControls[i]}] = windows[7+i].data();
    uiChildren[{root,"InvSlot22"}] = windows[14].data();
    uiChildren[{root,"IW_Destroy"}] = windows[15].data();
    for (int i=0;i<3;++i) uiChildren[{eqMain,EqMainHiddenControls[i]}] = windows[17+i].data();
    TickUiCleanup();
    Check(stringsMade == 0 && windowsHidden == 0, "cleanup touched login UI");
    state = 5; std::memcpy(eq.data()+0x5c8,&state,4);
    TickUiCleanup();
    std::memcpy(&count,tabs+0x1e0,4);
    Check(count == 2 && pages[0] == windows[2].data() && pages[1] == windows[3].data(), "Inventory/Stats tabs not retained");
    Check(pagesRemoved == 3, "requested inventory tabs were not removed");
    for (int i=4;i<=13;++i) Check(!windows[i][0x196], "requested inventory control still visible");
    for (int i=16;i<=19;++i) Check(!windows[i][0x196], "EQ/Marketplace panel control still visible");
    for (int i : {0,2,3,14,15}) Check(windows[i][0x196], "unrelated inventory control hidden");
    Check(stringsMade == stringsFreed, "native lookup strings leaked");
    const int hidden = windowsHidden;
    TickUiCleanup();
    Check(pagesRemoved == 3 && windowsHidden == hidden, "cleanup is not idempotent");
    // Reload a skin at the same object addresses; no stale cache may skip it.
    for (int i=0;i<5;++i) pages[i] = windows[i+2].data();
    count = 5; std::memcpy(tabs+0x1e0,&count,4);
    for (auto& window : windows) window[0x196] = 1;
    TickUiCleanup();
    Check(pagesRemoved == 6 && stringsMade == stringsFreed, "cleanup did not reapply after UI reload");
    inventory = nullptr; eqMain = nullptr;
    Write(image.At(0xd1fc8c,4),&inventory,4); Write(image.At(0xf713c0,4),&eqMain,4);
    TickUiCleanup();
    std::array<Window,13> character{};
    for(auto& window:character) {void* table=vtable.data();std::memcpy(window.data(),&table,4);window[0x196]=1;}
    void* select=character[0].data();void* create=character[1].data();
    Write(image.At(0xd1fc34,4),&select,4);Write(image.At(0xd1fc78,4),&create,4);
    for(int i=0;i<4;++i)uiChildren[{select,CharacterSelectHiddenControls[i]}]=character[2+i].data();
    for(int i=0;i<5;++i)uiChildren[{create,CharacterCreateHiddenControls[i]}]=character[6+i].data();
    uiChildren[{select,"EnterWorld_Button"}]=character[11].data();
    uiChildren[{create,"CC_Race_Human_Button"}]=character[12].data();
    state=1;std::memcpy(eq.data()+0x5c8,&state,4);
    using Draw=int(__thiscall *)(void*);auto draw=reinterpret_cast<Draw>(image.At(0x877600));
    Check(draw(eq.data())==77,"UI draw return value changed");
    for(int i=2;i<11;++i)Check(!character[i][0x196],"character-select offer or Drakkin control still visible");
    for(int i:{0,1,11,12})Check(character[i][0x196],"normal character selection/races hidden");
    Check(stringsMade==stringsFreed,"character cleanup leaked strings");
    select=nullptr;create=nullptr;Write(image.At(0xd1fc34,4),&select,4);Write(image.At(0xd1fc78,4),&create,4);
    VirtualFree(drawThunk,0,MEM_RELEASE);
    std::puts("PASS character-select pre-render cleanup: buy/gold/Drakkin hidden; Enter World and Human preserved");
    std::puts("PASS UI cleanup: three tabs, crystals/reclaim/plot/power-source, EQ/Marketplace panel, preserved ammo/Stats, UI reload and string lifecycle");
}

}

int main(int argc, char** argv)
{
    try {
        Check(argc <= 3, "usage: gusto_instance_gate_hook_tests.exe [eqgame.exe] [eqmain.dll]");
        Image image;
        image.Open(argc >= 2 ? argv[1] : "eqgame.exe");
        ValidationReport report;
        ValidatedClient token;
        if (!ValidateLoadedClient(image.module, report, token)) {
            std::fprintf(stderr, "validation status: %u\n", static_cast<unsigned>(report.status));
            Check(false, "unsupported client image");
        }
        PvpGuardTest(image, token);
        GateTest(image, token);
        if (argc == 3) ServerSelectTest(image, token, argv[2]);
        UiCleanupTest(image, token);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s (Win32 error %lu)\n", error.what(), GetLastError());
        return 1;
    }
}
