#include "gusto/client/rof2/nameplate_hooks.hpp"
#include "gusto/client/rof2/tag_hooks.hpp"
#include "gusto/client/rof2/hud_model.hpp"

#include "gusto/client/rof2/hook_utils.hpp"
#include "gusto/client/rof2/globals.hpp"
#include "gusto/client/rof2/addresses.hpp"
#include "gusto/client/rof2/initialization.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_set>
#include <unordered_map>
#include <array>
#include <cstdint>
#include <climits>
#include <cstdio>
#include <cstring>
#include <limits>

namespace gusto::client::rof2
{
    namespace
    {
        constexpr std::uintptr_t SetNameSpriteStateVA = 0x0058E2D0;
        constexpr std::uintptr_t SetNameSpriteTintVA = 0x0058BF00;
        constexpr std::uintptr_t ChangeBoneStringSpriteVA = 0x0058BE90;
        constexpr std::uintptr_t AutoRangeAttackSetterVA = 0x00520B40;
        constexpr std::uintptr_t AutoAttackSetterVA = 0x0052D4D0;

        constexpr BYTE StateExpected[] = {0xB8, 0xA8, 0x26, 0x00, 0x00};
        constexpr BYTE TintExpected[] = {0x83, 0xEC, 0x08, 0x57, 0x8B, 0xF9};
        constexpr BYTE ChangeBoneStringSpriteExpected[] =
            {0xB8, 0x00, 0x20, 0x00, 0x00};
        constexpr BYTE AttackSetterExpected[] =
            {0x8A, 0x44, 0x24, 0x04, 0x56};

        constexpr std::uintptr_t kPlayerActorOffset = 0x101C;
        constexpr std::uintptr_t kActorTintVtableOffset = 0x190;
        constexpr std::uintptr_t kCharacterTypeOffset = 0x125;
        constexpr std::uintptr_t kLevelOffset = 0x250;
        constexpr std::uintptr_t kDisplayNameSpriteOffset = 0x1228;

        constexpr BYTE kNpcType = 1;
        constexpr int kNameSpriteBone = 0;

        // RoF2 actor tint consumes color bytes in BGR order.
        struct NameplateBGR
        {
            std::uint8_t blue;
            std::uint8_t green;
            std::uint8_t red;
        };

        static_assert(sizeof(NameplateBGR) == 3);

        constexpr NameplateBGR kWhite{240, 240, 240};
        constexpr NameplateBGR kYellow{0, 240, 240};
        constexpr NameplateBGR kRed{0, 0, 240};
        constexpr NameplateBGR kBlue{240, 64, 0};
        constexpr NameplateBGR kLightBlue{240, 240, 0};
        constexpr NameplateBGR kGreen{0, 240, 0};
        constexpr NameplateBGR kPink{180, 80, 255};

        bool IsAttackingTargetNpc(void* self);
        std::atomic_bool conColors{true}, pinkTarget{true}, attackBrackets{true}, attackFlash{true};
        std::atomic<float> flashRate{15.5f};
        std::atomic_bool nameOptionsDirty{false}, nativePetOwners{true};
        const NameplateBGR* TargetTint(void* spawn) {
            if (attackFlash && IsAttackingTargetNpc(spawn) &&
                hud::AttackFlashWhite(GetTickCount64(),flashRate.load()))
                return &kWhite;
            return &kPink;
        }

        constexpr LONG kMaxLoggedCalls = 500;

        using SetNameSpriteStateFn = int(__thiscall*)(void*, bool);
        using SetNameSpriteTintFn = bool(__thiscall*)(void*);
        using SetActorTintFn = void(__thiscall*)(void*, const NameplateBGR*);
        using ChangeBoneStringSpriteFn =
            void(__thiscall*)(void*, int, const char*);
        using ActorNameSetterFn =
            void(__thiscall*)(void*, int, const char*, const char*);
        using AttackStateSetterFn = void(__thiscall*)(void*, bool);

        SetNameSpriteStateFn g_originalState = nullptr;
        thread_local void* currentNameSpawn=nullptr;
        struct NameplateScope {
            void* previous=currentNameSpawn;
            explicit NameplateScope(void* spawn) {currentNameSpawn=spawn;}
            ~NameplateScope(){currentNameSpawn=previous;}
        };
        int InvokeNameState(void* spawn,bool show) {
            NameplateScope scope(spawn);
            return g_originalState?g_originalState(spawn,show):0;
        }
        std::string PetOwnerFor(void* spawn);
        SetNameSpriteTintFn g_originalTint = nullptr;
        ChangeBoneStringSpriteFn g_originalChangeBoneStringSprite = nullptr;
        ActorNameSetterFn g_originalActorName = nullptr;
        AttackStateSetterFn g_originalAutoAttackSetter = nullptr;
        AttackStateSetterFn g_originalAutoRangeAttackSetter = nullptr;
        volatile LONG g_actorNameHookState = 0;

        bool IsAttackingTargetNpc(void* self);
        void __fastcall ActorNameSetterHook(
            void* actor, void*, int arg1, const char* text, const char* outputText);
        bool InstallActorNameHook(void** slot, void* original);


        HMODULE g_module = nullptr;
        ValidatedClient g_validation{};
        volatile LONG g_installed = 0;

        bool IsReadableRange(const void* address, std::size_t length)
        {
            if (!address || length == 0)
                return false;

            const auto start = reinterpret_cast<std::uintptr_t>(address);

            if (length > std::numeric_limits<std::uintptr_t>::max() - start)
                return false;

            const auto end = start + length;
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

                const auto regionEnd = regionBase + info.RegionSize;

                if (regionEnd <= current)
                    return false;

                current = (std::min)(static_cast<std::uintptr_t>(regionEnd), end);
            }

            return true;
        }

        bool IsExecutableAddress(const void* address)
        {
            if (!address)
                return false;

            MEMORY_BASIC_INFORMATION info{};

            if (!VirtualQuery(address, &info, sizeof(info)))
                return false;

            if (info.State != MEM_COMMIT ||
                (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                return false;

            const DWORD protection = info.Protect & 0xFF;

            return protection == PAGE_EXECUTE ||
                   protection == PAGE_EXECUTE_READ ||
                   protection == PAGE_EXECUTE_READWRITE ||
                   protection == PAGE_EXECUTE_WRITECOPY;
        }

        template <typename T>
        bool ReadValue(const void* address, T& value)
        {
            if (!IsReadableRange(address, sizeof(T)))
                return false;

            std::memcpy(&value, address, sizeof(T));
            return true;
        }





        const NameplateBGR* GetNpcConColor(int playerLevel, int npcLevel)
        {
            const int difference = npcLevel - playerLevel;

            if (difference == 0)
                return &kWhite;

            if (difference > 0)
                return difference <= 3 ? &kYellow : &kRed;

            int greenThreshold = -16;
            int lightBlueThreshold = -11;

            if (playerLevel <= 7)
            {
                greenThreshold = -4;
                lightBlueThreshold = -4;
            }
            else if (playerLevel <= 8)
            {
                greenThreshold = -5;
                lightBlueThreshold = -4;
            }
            else if (playerLevel <= 12)
            {
                greenThreshold = -6;
                lightBlueThreshold = -4;
            }
            else if (playerLevel <= 16)
            {
                greenThreshold = -7;
                lightBlueThreshold = -5;
            }
            else if (playerLevel <= 20)
            {
                greenThreshold = -8;
                lightBlueThreshold = -6;
            }
            else if (playerLevel <= 24)
            {
                greenThreshold = -9;
                lightBlueThreshold = -7;
            }
            else if (playerLevel <= 28)
            {
                greenThreshold = -10;
                lightBlueThreshold = -8;
            }
            else if (playerLevel <= 30)
            {
                greenThreshold = -11;
                lightBlueThreshold = -9;
            }
            else if (playerLevel <= 32)
            {
                greenThreshold = -12;
                lightBlueThreshold = -9;
            }
            else if (playerLevel <= 36)
            {
                greenThreshold = -13;
                lightBlueThreshold = -10;
            }
            else if (playerLevel <= 40)
            {
                greenThreshold = -11;
                lightBlueThreshold = -6;
            }
            else if (playerLevel <= 44)
            {
                greenThreshold = -16;
                lightBlueThreshold = -12;
            }
            else if (playerLevel <= 48)
            {
                greenThreshold = -17;
                lightBlueThreshold = -13;
            }
            else if (playerLevel <= 52)
            {
                greenThreshold = -18;
                lightBlueThreshold = -14;
            }
            else if (playerLevel <= 54)
            {
                greenThreshold = -19;
                lightBlueThreshold = -15;
            }
            else if (playerLevel <= 56)
            {
                greenThreshold = -20;
                lightBlueThreshold = -15;
            }
            else if (playerLevel <= 60)
            {
                greenThreshold = -21;
                lightBlueThreshold = -16;
            }
            else if (playerLevel <= 61)
            {
                greenThreshold = -19;
                lightBlueThreshold = -14;
            }
            else if (playerLevel <= 62)
            {
                greenThreshold = -17;
                lightBlueThreshold = -12;
            }

            if (difference <= greenThreshold)
                return &kGreen;

            if (difference <= lightBlueThreshold)
                return &kLightBlue;

            return &kBlue;
        }

        void ApplyNpcConColor(void* self)
        {
            if (!self || !g_module || !g_validation.IsValidFor(g_module))
                return;

            // Validate the PlayerClient object before reading its fields.
            if (!IsReadableRange(
                self, addresses::PlayerClientReferenceSize))
                return;

            const auto selfAddress = reinterpret_cast<std::uintptr_t>(self);

            BYTE characterType = 0;
            BYTE npcLevel = 0;

            if (!ReadValue(
                reinterpret_cast<const void*>(
                    selfAddress + kCharacterTypeOffset),
                characterType))
                return;

            // Do not alter player characters or corpses.
            if (characterType != kNpcType)
                return;

            if (!ReadValue(
                reinterpret_cast<const void*>(selfAddress + kLevelOffset),
                npcLevel))
                return;

            void* localPlayer = ResolvePointer(
                g_module,
                g_validation,
                addresses::LocalPlayer,
                addresses::PlayerClientReferenceSize);

            if (!localPlayer)
                return;

            BYTE playerLevel = 0;

            if (!ReadValue(
                reinterpret_cast<const BYTE*>(localPlayer) + kLevelOffset,
                playerLevel))
                return;

            // Zero is not a usable character level.
            if (playerLevel == 0 || npcLevel == 0)
                return;

            void* actor = nullptr;

            if (!ReadValue(
                reinterpret_cast<const void*>(
                    selfAddress + kPlayerActorOffset),
                actor) ||
                !actor)
                return;

            void** vtable = nullptr;

            if (!ReadValue(actor, vtable) || !vtable)
                return;

            void* nameFunction = nullptr;
            void* tintFunction = nullptr;
            const auto* slots = reinterpret_cast<void* const*>(vtable);
            if (!ReadValue(slots + (0x18C / sizeof(void*)), nameFunction) ||
                !ReadValue(slots + (0x190 / sizeof(void*)), tintFunction) ||
                !IsExecutableAddress(nameFunction) ||
                !IsExecutableAddress(tintFunction))
                return;
            auto** nameSlot =
                reinterpret_cast<void**>(vtable) + (0x18C / sizeof(void*));
            InstallActorNameHook(nameSlot, nameFunction);

            const auto slotAddress =
                reinterpret_cast<const std::uintptr_t*>(vtable) +
                (kActorTintVtableOffset / sizeof(void*));

            void* functionAddress = nullptr;

            if (!ReadValue(slotAddress, functionAddress) ||
                !IsExecutableAddress(functionAddress))
                return;

            const ClientState* clientState = GetClientState();
            const bool isTarget =
                clientState &&
                clientState->GetTarget() ==
                    reinterpret_cast<PlayerClient*>(self);

            if (!(isTarget && pinkTarget) && !conColors) return;
            const NameplateBGR* color = (isTarget && pinkTarget)
                ? TargetTint(self) : GetNpcConColor(playerLevel, npcLevel);

            const auto setActorTint =
                reinterpret_cast<SetActorTintFn>(functionAddress);

            setActorTint(actor, color);
        }

        // __fastcall with the 3rd parameter on the stack matches a thiscall
        // with one stack argument.
        int __fastcall SetNameSpriteStateHook(
            void* self, void*, bool show)
        {


            TickTags();
            return InvokeNameState(self,show);
        }

        // No stack arguments: matches a thiscall with no arguments.
        bool __fastcall SetNameSpriteTintHook(void* self, void*)
        {


            const auto original = g_originalTint;
            const bool result = original ? original(self) : false;

            // Preserve the client's normal tint unless this is a valid NPC.
            ApplyNpcConColor(self);

            return result;
        }

        bool IsAttackingTargetNpc(void* self)
        {
            if (!self || !g_module || !g_validation.IsValidFor(g_module))
                return false;

            const ClientState* clientState = GetClientState();
            if (!clientState)
                return false;

            PlayerClient* target = clientState->GetTarget();
            if (!target || target != self)
                return false;

            const auto* targetBytes = reinterpret_cast<const BYTE*>(target);
            BYTE characterType = 0;

            if (!IsReadableRange(
                    targetBytes, addresses::PlayerClientReferenceSize) ||
                !ReadValue(targetBytes + kCharacterTypeOffset, characterType) ||
                characterType != kNpcType)
                return false;

            EverQuestInfo* info = clientState->GetEverQuestInfo();
            if (!info)
                return false;

            const auto* infoBytes = reinterpret_cast<const BYTE*>(info);
            BYTE autoAttack = 0;
            BYTE autoRangeAttack = 0;

            if (!IsReadableRange(infoBytes + 0x83F92, 2) ||
                !ReadValue(infoBytes + 0x83F92, autoAttack) ||
                !ReadValue(infoBytes + 0x83F93, autoRangeAttack))
                return false;

            return autoAttack != 0 || autoRangeAttack != 0;
        }

        void __fastcall ActorNameSetterHook(
            void* actor, void*, int arg1, const char* text, const char* outputText)
        {
            const auto original = g_originalActorName;
            if (!original)
                return;
            if (arg1!=kNameSpriteBone) { original(actor,arg1,text,outputText); return; }

            const std::string tag=GetActorTagText(actor);
            const char* replacement=outputText;
            std::string formatted;
            if (outputText) {
                std::size_t length=0;
                while (length<480 && IsReadableRange(outputText+length,1) && outputText[length]) ++length;
                if (length<480 && IsReadableRange(outputText+length,1)) {
                    const auto* state=GetClientState();
                    void* target=state?state->GetTarget():nullptr;
                    void* targetActor=nullptr;
                    bool matches=target && ReadValue(static_cast<const BYTE*>(target)+kPlayerActorOffset,targetActor) && targetActor==actor;
                    auto base=std::string_view(outputText,length);
                    const auto prefix=tag+"\n";
                    if (!tag.empty() && base.starts_with(prefix)) base.remove_prefix(prefix.size());
                    std::string body(base);
                    void* contextActor=nullptr; BYTE contextType=255;
                    if (currentNameSpawn &&
                        ReadValue(static_cast<const BYTE*>(currentNameSpawn)+kPlayerActorOffset,contextActor) && contextActor==actor &&
                        ReadValue(static_cast<const BYTE*>(currentNameSpawn)+kCharacterTypeOffset,contextType) && contextType==1)
                        body=hud::PetNameplate(body,PetOwnerFor(currentNameSpawn));
                    formatted=hud::AttackNameplate(body,attackBrackets && matches && IsAttackingTargetNpc(target));
                    replacement=formatted.c_str();
                }
            }

            std::string tagged;
            if (!tag.empty() && replacement) {
                std::size_t length = 0;
                while (length < 480 && IsReadableRange(replacement + length, 1) && replacement[length]) ++length;
                if (length < 480 && IsReadableRange(replacement + length, 1) && !replacement[length]) {
                    const std::string prefix = tag + "\n";
                    if (std::strncmp(replacement, prefix.c_str(), prefix.size()) != 0) {
                        tagged = prefix + replacement;
                        replacement = tagged.c_str();
                    }
                }
            }
            original(actor, arg1, text, replacement);
        }

        bool InstallActorNameHook(void** slot, void* original)
        {
            if (!slot || !original ||
                !IsReadableRange(slot, sizeof(void*)) ||
                !IsExecutableAddress(original))
                return false;

            const LONG state =
                InterlockedCompareExchange(&g_actorNameHookState, 1, 0);
            if (state != 0)
                return state == 2;

            g_originalActorName =
                reinterpret_cast<ActorNameSetterFn>(original);

            DWORD oldProtection = 0;
            if (!VirtualProtect(
                    slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtection))
            {
                g_originalActorName = nullptr;
                InterlockedExchange(&g_actorNameHookState, 0);
                return false;
            }

            void* previous = InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(slot),
                reinterpret_cast<PVOID>(&ActorNameSetterHook),
                original);

            DWORD ignoredProtection = 0;
            const BOOL restored =
                VirtualProtect(slot, sizeof(void*), oldProtection, &ignoredProtection);

            const bool installed =
                previous == original ||
                previous == reinterpret_cast<void*>(&ActorNameSetterHook);

            if (installed)
            {
                InterlockedExchange(&g_actorNameHookState, 2);

                return true;
            }

            g_originalActorName = nullptr;
            InterlockedExchange(&g_actorNameHookState, 0);
            return false;
        }

        void RefreshTargetNpcNameplate()
        {
            const ClientState* clientState = GetClientState();
            if (!clientState || !g_originalState)
                return;

            PlayerClient* target = clientState->GetTarget();
            if (!target)
                return;

            const auto* bytes = reinterpret_cast<const BYTE*>(target);
            BYTE characterType = 0;

            if (!IsReadableRange(
                    bytes, addresses::PlayerClientReferenceSize) ||
                !ReadValue(bytes + kCharacterTypeOffset, characterType) ||
                characterType != kNpcType)
                return;

            void* targetActor = nullptr;
            if (!ReadValue(bytes + kPlayerActorOffset, targetActor) ||
                !targetActor)
                return;

            InvokeNameState(target, true);
        }

        void __fastcall AutoAttackSetterHook(void* self, void*, bool enabled)
        {
            const auto original = g_originalAutoAttackSetter;
            if (original)
                original(self, enabled);
        }

        void __fastcall AutoRangeAttackSetterHook(void* self, void*, bool enabled)
        {
            const auto original = g_originalAutoRangeAttackSetter;
            if (original)
                original(self, enabled);
        }





        bool IsAttackingTargetNpcSprite(void* sprite)
        {
            return IsAttackingTargetNpc(sprite);
        }

        std::string PetOwnerFor(void* spawn) {
            if (!nativePetOwners || !g_validation.IsValidFor(g_module)) return {};
            BYTE type=255; std::uint32_t owner=0;
            if (!ReadValue(static_cast<const BYTE*>(spawn)+kCharacterTypeOffset,type) || type!=1 ||
                !ReadValue(static_cast<const BYTE*>(spawn)+0x38c,owner) || !owner || owner>INT_MAX) return {};
            void* manager=ResolvePointer(g_module,g_validation,{0xe641d0,addresses::Storage::PointerStorage},0x18);
            const auto address=ResolvePreferredVA(g_module,0x5996e0,5);
            if (!manager || !address) return {};
            using LookupFn=void*(__thiscall*)(void*,int);
            void* player=reinterpret_cast<LookupFn>(address)(manager,int(owner));
            if (!player || !ReadValue(static_cast<const BYTE*>(player)+kCharacterTypeOffset,type) || type!=0) return {};
            std::array<char,64> name{};
            if (!ReadValue(static_cast<const BYTE*>(player)+0xa4,name)) return {};
            const auto end=std::find(name.begin(),name.end(),'\0');
            return end==name.end()?std::string{}:std::string(name.begin(),end);
        }
        void __fastcall ChangeBoneStringSpriteHook(void* self,void*,int bone,const char* text) {
            const auto original=g_originalChangeBoneStringSprite;
            if (!original) return;
            NameplateScope scope(self);
            if (bone!=kNameSpriteBone || !text) { original(self,bone,text); return; }
            std::size_t length=0;
            while (length<480 && IsReadableRange(text+length,1) && text[length]) ++length;
            if (length==480 || !IsReadableRange(text+length,1)) { original(self,bone,text); return; }
            BYTE type=255;
            std::string result(text,length);
            if (ReadValue(static_cast<const BYTE*>(self)+kCharacterTypeOffset,type) && type==1)
                result=hud::PetNameplate(result,PetOwnerFor(self));
            result=hud::AttackNameplate(result,attackBrackets && IsAttackingTargetNpc(self));
            original(self,bone,result.c_str());
        }



    }

    NameplateOptions GetNameplateOptions() {
        return {conColors.load(), pinkTarget.load(), attackBrackets.load(),
                attackFlash.load(), flashRate.load()};
    }
    void SetNameplateOptions(const NameplateOptions& o) {
        conColors=o.conColors; pinkTarget=o.pinkTarget; attackBrackets=o.brackets;
        attackFlash=o.attackFlash;
        flashRate=std::isfinite(o.flashChangesPerSecond)
            ? std::clamp(o.flashChangesPerSecond, 1.f, 16.f) : 15.5f;
        nameOptionsDirty=true;
    }
    void SetNativePetOwnerNames(bool enabled) {
        if (nativePetOwners.exchange(enabled)!=enabled) nameOptionsDirty=true;
    }
    void TickNameplateOptions() {
        if (!g_installed || !g_validation.IsValidFor(g_module)) return;
        const auto* state=GetClientState();
        if (!state) return;
        void* target=state->GetTarget();
        static void* oldTarget=nullptr;
        static bool oldAttack=false;
        const bool attacking=IsAttackingTargetNpc(target);
        const bool changed=nameOptionsDirty.exchange(false);
        static ULONGLONG petTick=0;
        static std::unordered_map<void*,std::pair<std::uint32_t,std::uint32_t>> owners;
        const auto now=GetTickCount64();
        const bool checkPets=now-petTick>=500;
        if (checkPets) petTick=now;
        const bool targetChanged=target!=oldTarget || attacking!=oldAttack;
        if (changed || targetChanged || checkPets) {
            // Only invoke native code on objects still in the live spawn list.
            void* manager=ResolvePointer(g_module,g_validation,
                {0xe641d0,addresses::Storage::PointerStorage},0x18);
            void* spawn=nullptr;
            if (manager) ReadValue(static_cast<const BYTE*>(manager)+8,spawn);
            std::unordered_set<void*> seen;
            while (spawn && seen.size()<4096 && seen.insert(spawn).second &&
                   IsReadableRange(spawn,addresses::PlayerClientReferenceSize)) {
                void* next=nullptr;
                if (!ReadValue(static_cast<const BYTE*>(spawn)+8,next)) break;
                BYTE type=255; ReadValue(static_cast<const BYTE*>(spawn)+kCharacterTypeOffset,type);
                std::uint32_t id=0,owner=0;
                ReadValue(static_cast<const BYTE*>(spawn)+0x148,id);
                ReadValue(static_cast<const BYTE*>(spawn)+0x38c,owner);
                const auto identity=std::make_pair(id,owner);
                const auto old=owners.find(spawn);
                const bool ownerChanged=type==kNpcType &&
                    ((owner && (old==owners.end() || old->second!=identity)) ||
                     (!owner && old!=owners.end() && old->second.second));
                if (type==kNpcType) owners[spawn]=identity;
                if (type==kNpcType && (changed || ownerChanged || spawn==target || spawn==oldTarget)) {
                    if (g_originalTint) g_originalTint(spawn);
                    ApplyNpcConColor(spawn);
                    if ((spawn==target || spawn==oldTarget || ownerChanged || (changed && owner)) && g_originalState)
                        InvokeNameState(spawn,true);
                }
                spawn=next;
            }
            std::erase_if(owners,[&](const auto& pair){return !seen.contains(pair.first);});
        }
        oldTarget=target; oldAttack=attacking;
        if (target && IsAttackingTargetNpc(target)) ApplyNpcConColor(target);
    }

    std::uint32_t GetNameplateTagColor(void* spawn)
    {
        const ClientState* state = GetClientState();
        BYTE npcLevel = 0, playerLevel = 0;
        const NameplateBGR* color = &kWhite;
        if (state && spawn == state->GetTarget() && pinkTarget) color = TargetTint(spawn);
        else if (conColors && state && spawn &&
            ReadValue(static_cast<const BYTE*>(spawn) + kLevelOffset, npcLevel) &&
            state->GetLocalPlayer() &&
            ReadValue(reinterpret_cast<const BYTE*>(state->GetLocalPlayer()) + kLevelOffset, playerLevel) &&
            npcLevel && playerLevel)
            color = GetNpcConColor(playerLevel, npcLevel);
        return 0xff000000u | (std::uint32_t(color->red) << 16) |
            (std::uint32_t(color->green) << 8) | color->blue;
    }

    bool InstallNameplateHooks(
        HMODULE module,
        const ValidatedClient& validation)
    {


        if (InterlockedCompareExchange(&g_installed, 0, 0) != 0)
            return true;

        if (!module || !validation.IsValidFor(module))
            return false;

        g_module = module;
        g_validation = validation;

        void* state = nullptr;
        void* tint = nullptr;

        if (!InstallDetour(
            module, validation,
            SetNameSpriteStateVA,
            StateExpected, sizeof(StateExpected),
            reinterpret_cast<const void*>(&SetNameSpriteStateHook),
            &state))
            return false;

        g_originalState = reinterpret_cast<SetNameSpriteStateFn>(state);


        if (!InstallDetour(
            module, validation,
            SetNameSpriteTintVA,
            TintExpected, sizeof(TintExpected),
            reinterpret_cast<const void*>(&SetNameSpriteTintHook),
            &tint))
        {

            return false;
        }

        g_originalTint = reinterpret_cast<SetNameSpriteTintFn>(tint);

        void* changeBone = nullptr;

        if (!InstallDetour(
            module, validation,
            ChangeBoneStringSpriteVA,
            ChangeBoneStringSpriteExpected,
            sizeof(ChangeBoneStringSpriteExpected),
            reinterpret_cast<const void*>(&ChangeBoneStringSpriteHook),
            &changeBone))
        {


            return false;
        }
        g_originalChangeBoneStringSprite =
            reinterpret_cast<ChangeBoneStringSpriteFn>(changeBone);


        void* autoAttack = nullptr;
        if (!InstallDetour(
            module, validation, AutoAttackSetterVA,
            AttackSetterExpected, sizeof(AttackSetterExpected),
            reinterpret_cast<const void*>(&AutoAttackSetterHook),
            &autoAttack))
            return false;
        g_originalAutoAttackSetter =
            reinterpret_cast<AttackStateSetterFn>(autoAttack);

        void* autoRangeAttack = nullptr;
        if (!InstallDetour(
            module, validation, AutoRangeAttackSetterVA,
            AttackSetterExpected, sizeof(AttackSetterExpected),
            reinterpret_cast<const void*>(&AutoRangeAttackSetterHook),
            &autoRangeAttack))
            return false;
        g_originalAutoRangeAttackSetter =
            reinterpret_cast<AttackStateSetterFn>(autoRangeAttack);

        InterlockedExchange(&g_installed, 1);
        return true;
    }
}
