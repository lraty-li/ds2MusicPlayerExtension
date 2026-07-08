#include "pch.h"
#include "BoardingClipSuppressor.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace BoardingClipSuppressor {
namespace {

constexpr const char* kOnEnterSignature =
    "48 8B C4 48 89 58 ? 48 89 70 ? 57 41 56 41 57 48 81 EC"
    " ? ? ? ? C5 F8 29 70 ? C5 F8 29 78 ? C5 78 29 40 ? 45 33 FF";
constexpr size_t kOnEnterPatchLen = 33;

// RideRuntime_UpdateSeatAnimationData - writes seat+0x1268 blend value
constexpr const char* kSeatAnimDataSignature =
    "48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC ? 48 8B 41";
constexpr size_t kSeatAnimDataPatchLen = 15;

using OnEnterFn = int64_t(__fastcall*)(uintptr_t rideOn);
using SeatAnimDataFn = char(__fastcall*)(uintptr_t rideRuntime);

std::atomic<bool> g_started{false};
std::atomic<int> g_enteredCount{0};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
OnEnterFn g_originalOnEnter = nullptr;
SeatAnimDataFn g_originalSeatAnimData = nullptr;

uintptr_t FindPattern(const char* sig)
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize))
        return 0;
    return PatternScan::Find(textStart, textSize, sig);
}

int64_t __fastcall HookOnEnter(uintptr_t rideOn)
{
    // Save inner+0x54 and write 2 to skip RebuildTrackSlots (state 5 path)
    uintptr_t animComp = 0;
    uintptr_t inner = 0;
    uint32_t savedV54 = 0;
    bool haveInner = false;
    
    if (VehicleSeatTrace::ReadValue(rideOn + 0xB0, animComp) && animComp) {
        if (VehicleSeatTrace::ReadValue(animComp + 0x8, inner) && inner) {
            haveInner = VehicleSeatTrace::ReadValue(inner + 0x54, savedV54);
        }
    }

    if (haveInner) {
        VehicleSeatTrace::WriteValue<uint32_t>(inner + 0x54, 2);
    }

    // Call original OnEnter (with RebuildTrackSlots skipped)
    const int64_t result = g_originalOnEnter(rideOn);

    // Restore inner+0x54
    if (haveInner) {
        uint32_t afterV54 = 0;
        VehicleSeatTrace::ReadValue(inner + 0x54, afterV54);
        VehicleSeatTrace::WriteValue<uint32_t>(inner + 0x54, savedV54);

        // Also log the animation inner state after OnEnter
        uint32_t v2E0 = 0;
        uint32_t v398 = 0;
        uint32_t v3A0 = 0;
        VehicleSeatTrace::ReadValue(inner + 0x2E0, v2E0);
        VehicleSeatTrace::ReadValue(inner + 0x398, v398);
        VehicleSeatTrace::ReadValue(inner + 0x3A0, v3A0);

        if (g_enteredCount.fetch_add(1) < 3) {
            std::ostringstream oss;
            oss << "OnEnter inner=" << VehicleSeatTrace::Hex(inner)
                << " v54=" << savedV54 << "->2->" << afterV54
                << " state=" << v2E0
                << " v398=" << v398 << " v3A0=" << v3A0;
            g_logger->Log(oss.str());
        }
    }

    return result;
}

} // anonymous namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;

    const uintptr_t onEnter = FindPattern(kOnEnterSignature);
    if (!onEnter) {
        logger.Log("OnEnter signature not found");
        return false;
    }

    std::ostringstream oss;
    oss << "OnEnter resolved at " << VehicleSeatTrace::Hex(onEnter);
    logger.Log(oss.str());

    void* trampoline = JumpHook::MakeTrampoline(onEnter, kOnEnterPatchLen);
    if (!trampoline) {
        logger.Log("OnEnter trampoline failed");
        return false;
    }

    g_originalOnEnter = reinterpret_cast<OnEnterFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            onEnter, reinterpret_cast<void*>(&HookOnEnter),
            kOnEnterPatchLen)) {
        logger.Log("OnEnter hook failed");
        return false;
    }

    logger.Log("BoardingClipSuppressor v0.5.0 installed");
    return true;
}

} // namespace BoardingClipSuppressor
