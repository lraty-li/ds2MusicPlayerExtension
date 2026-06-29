#include "pch.h"
#include "VehicleSeatTrace.h"
#include "HookUtils.h"
#include "PatternScan.h"
#include <sstream>

namespace {

// sub_1402F1EF0 (StartMount) - NPC boarding entry
const char* kStartMountPattern =
    "40 57 48 83 EC ? 80 79 ? ? 48 8B F9 0F 84 ? ? ? ? 0F B6 44 24";

// sub_140EF1E70 (DSActionPlugin vtable[9])
const char* kActionCheckPattern =
    "40 53 48 83 EC ? 80 79 ? ? 48 8B D9 75 ? 48 8B 01 FF 50";

// sub_1410047B0: RideVehicleActionPlugin::Init
// THE boarding entry point — sets *(WORD*)(plugin+282)=1 to trigger RideOn
// Prologue: push rbx; sub rsp, 90h = 8 bytes  (detourable)
// Signature verified by IDA: unique
const char* kInitPattern =
    "40 53 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 8B D9"
    " 80 B8 ? ? ? ? ? 0F 85 ? ? ? ? 48 8B 49";

using StartMountFn = void(__fastcall*)(__int64, __int64, __int64, __int64, char);
using ActionCheckFn = bool(__fastcall*)(void*);
using InitFn = char(__fastcall*)(void*);

constexpr uint32_t kPatchBytes = 13;
constexpr uint32_t kInitPatchBytes = 16;  // Init: push(2)+sub(7)+mov_rip(7)=16
  // push rbx = 40 53 (2B, REX prefix!)
  // sub rsp,90h = 48 81 EC 90 00 00 00 (7B)
  // mov rax,[rip+X] = 48 8B 05 XX XX XX XX (7B, disp at +12)

Logger* g_logger = nullptr;
StartMountFn g_startMountOriginal = nullptr;
ActionCheckFn g_actionCheckOriginal = nullptr;
InitFn g_initOriginal = nullptr;
uintptr_t g_gameBase = 0;

void Log(const std::string& text) { if (g_logger) g_logger->Log(text); }

void WriteAbsoluteJump(uint8_t* target, void* dest) {
    target[0] = 0x48; target[1] = 0xB8;
    *reinterpret_cast<void**>(target + 2) = dest;
    target[10] = 0xFF; target[11] = 0xE0;
}

bool InstallDetour(uintptr_t target, void* detour, void** original) {
    auto* gateway = static_cast<uint8_t*>(VirtualAlloc(nullptr,
        kPatchBytes + 12, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!gateway) return false;
    memcpy(gateway, reinterpret_cast<void*>(target), kPatchBytes);
    WriteAbsoluteJump(gateway + kPatchBytes,
        reinterpret_cast<void*>(target + kPatchBytes));
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(target), kPatchBytes,
        PAGE_EXECUTE_READWRITE, &old)) return false;
    auto* patch = reinterpret_cast<uint8_t*>(target);
    WriteAbsoluteJump(patch, detour);
    patch[12] = 0x90;
    VirtualProtect(patch, kPatchBytes, old, &old);
    FlushInstructionCache(GetCurrentProcess(), patch, kPatchBytes);
    *original = gateway;
    return true;
}

// Special 16-byte detour for Init with RIP-relative fixup
// Init prologue (16 bytes):
//   40 53              push rbx           (2B, REX prefix!)
//   48 81 EC 90 00 00 00  sub rsp, 0x90     (7B)
//   48 8B 05 XX XX XX XX  mov rax, [rip+X]  (7B, disp at offset 12)
bool InstallInitDetour(uintptr_t target, void* detour, void** original) {
    constexpr size_t kGateSize = kInitPatchBytes + 14;
    constexpr int kRipDispOff = 12;  // displacement in mov rax,[rip+X]

    // Allocate gateway near target so RIP-relative offset fits in int32
    uint8_t* gateway = nullptr;
    for (int step = 0; step < 64; ++step) {
        int64_t delta64 = -(int64_t)(step * 0x1000000LL);
        uintptr_t hint = (uintptr_t)((int64_t)target + delta64);
        if (hint > target) break;
        gateway = static_cast<uint8_t*>(VirtualAlloc(
            reinterpret_cast<void*>(hint), kGateSize,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (gateway) break;
    }
    if (!gateway) return false;

    memcpy(gateway, reinterpret_cast<void*>(target), kInitPatchBytes);

    // Fix RIP-relative displacement at gateway + kRipDispOff
    int32_t origDisp = *reinterpret_cast<int32_t*>(
        reinterpret_cast<uint8_t*>(target) + kRipDispOff);
    int64_t fullDelta = (int64_t)target - (int64_t)gateway;
    *reinterpret_cast<int32_t*>(gateway + kRipDispOff) =
        origDisp + (int32_t)fullDelta;

    WriteAbsoluteJump(gateway + kInitPatchBytes,
        reinterpret_cast<void*>(target + kInitPatchBytes));
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(target), kInitPatchBytes,
        PAGE_EXECUTE_READWRITE, &old)) return false;
    auto* patch = reinterpret_cast<uint8_t*>(target);
    WriteAbsoluteJump(patch, detour);
    VirtualProtect(patch, kInitPatchBytes, old, &old);
    FlushInstructionCache(GetCurrentProcess(), patch, kInitPatchBytes);
    *original = gateway;
    return true;
}

void __fastcall StartMountDetour(__int64 a1, __int64 a2, __int64 a3, __int64 a4, char a5) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    std::ostringstream oss;
    oss << "StartMount: a1=" << HookUtils::HexU64(a1)
        << " a2=" << HookUtils::HexU64(a2)
        << " caller=" << HookUtils::HexU64(caller - g_gameBase);
    Log(oss.str());
    g_startMountOriginal(a1, a2, a3, a4, a5);
}

bool __fastcall ActionCheckDetour(void* a1) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    std::ostringstream oss;
    oss << "ActionCheck: a1=" << HookUtils::HexU64((uint64_t)a1)
        << " caller=" << HookUtils::HexU64(caller - g_gameBase);
    Log(oss.str());
    return g_actionCheckOriginal(a1);
}

char __fastcall InitDetour(void* a1) {
    // Log IMMEDIATELY to confirm we entered the detour
    Log("Init: ENTERED");

    // Call original Init — this sets next_state to 1 (RideOn) if boarding
    char result = g_initOriginal(a1);

    uint8_t* plugin = (uint8_t*)a1;
    uint8_t oldNext = plugin[282];
    uint8_t oldFlag = plugin[283];
    std::ostringstream oss;
    oss << "Init: plugin=" << HookUtils::HexU64((uint64_t)a1)
        << " result=" << (int)result
        << " oldNext=" << (int)oldNext
        << " oldFlag=" << (int)oldFlag;
    Log(oss.str());

    if (result == 1) {
        if (oldNext == 1) {
            plugin[282] = 2;
            plugin[283] = 0;
            Log("  -> Redirect: RideOn(1) -> Drive(2)");
        }
    }

    return result;
}

} // anonymous

namespace VehicleSeatTrace {

bool TryInstall(HMODULE gameModule, const Logger& logger) {
    g_logger = const_cast<Logger*>(&logger);
    g_gameBase = (uintptr_t)gameModule;
    DWORD imageSize = 0;
    HookUtils::TryGetModuleSize(gameModule, imageSize);
    uintptr_t base = g_gameBase;
    size_t size = imageSize;

    uintptr_t sm = 0; //PatternScan::Find(base, size, kStartMountPattern);
    uintptr_t ac = 0; //PatternScan::Find(base, size, kActionCheckPattern);
    uintptr_t init = PatternScan::Find(base, size, kInitPattern);
    bool ok = true;

    if (sm) {
        std::ostringstream o; o << "StartMount at " << HookUtils::HexU64(sm - g_gameBase); Log(o.str());
        ok |= InstallDetour(sm, (void*)&StartMountDetour, (void**)&g_startMountOriginal);
    }
    if (ac) {
        std::ostringstream o; o << "ActionCheck at " << HookUtils::HexU64(ac - g_gameBase); Log(o.str());
        ok |= InstallDetour(ac, (void*)&ActionCheckDetour, (void**)&g_actionCheckOriginal);
    }
    if (init) {
        std::ostringstream o; o << "Init at " << HookUtils::HexU64(init - g_gameBase); Log(o.str());
        ok |= InstallInitDetour(init, (void*)&InitDetour, (void**)&g_initOriginal);
    } else {
        Log("Init NOT FOUND");
    }

    if (!ok) { Log("no hooks"); return false; }
    Log("hooks installed");
    return true;
}

} // namespace VehicleSeatTrace
