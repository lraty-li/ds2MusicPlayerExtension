#include "pch.h"
#include "RideOffPoseTrace.h"
#include "JumpHook.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace RideOffPoseTrace {
namespace {

constexpr uintptr_t kImageBase = 0x140000000ull;
constexpr uintptr_t kRideOffPoseVariantRva = 0x140F98A40ull - kImageBase;
constexpr uintptr_t kDismountSideClassifyRva = 0x14100FF60ull - kImageBase;
constexpr size_t kRideOffPoseVariantPatchLen = 20;
constexpr size_t kDismountSideClassifyPatchLen = 17;

using RideOffPoseVariantFn = uint8_t(__fastcall*)(uintptr_t rideOff, uint8_t side);
using DismountSideClassifyFn = uint8_t(__fastcall*)(
    uintptr_t runtime, uintptr_t objectTag, uintptr_t tagRange);

std::atomic<bool> g_started{false};
std::atomic<int> g_logBudget{80};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
RideOffPoseVariantFn g_originalRideOffPoseVariant = nullptr;
DismountSideClassifyFn g_originalDismountSideClassify = nullptr;

template <typename T>
T ReadOr(uintptr_t addr, T fallback)
{
    T value = fallback;
    VehicleSeatTrace::ReadValue(addr, value);
    return value;
}

uint8_t __fastcall HookRideOffPoseVariant(uintptr_t rideOff, uint8_t side)
{
    const uint8_t result = g_originalRideOffPoseVariant(rideOff, side);
    const int remaining = g_logBudget.fetch_sub(1);
    if (remaining > 0) {
        const uintptr_t runtime = ReadOr<uintptr_t>(rideOff + 0x190, 0);
        const uint32_t kind = runtime ? ReadOr<uint32_t>(runtime + 0x2A0, 0) : 0;
        const uint32_t mountVariant =
            runtime ? ReadOr<uint32_t>(runtime + 0x2A4, 0xFFFFFFFFu) : 0xFFFFFFFFu;
        std::ostringstream oss;
        oss << "RideOffPoseVariant side=" << static_cast<int>(side)
            << " result=" << static_cast<int>(result)
            << " rideOff=" << VehicleSeatTrace::Hex(rideOff)
            << " runtime=" << VehicleSeatTrace::Hex(runtime)
            << " kind=" << kind
            << " runtime+2A4=" << mountVariant;
        g_logger->Log(oss.str());
    }
    return result;
}

uint8_t __fastcall HookDismountSideClassify(
    uintptr_t runtime, uintptr_t objectTag, uintptr_t tagRange)
{
    const uint8_t result =
        g_originalDismountSideClassify(runtime, objectTag, tagRange);
    const int remaining = g_logBudget.fetch_sub(1);
    if (remaining > 0) {
        const uint32_t kind = ReadOr<uint32_t>(runtime + 0x2A0, 0);
        const uintptr_t owner = ReadOr<uintptr_t>(runtime + 0x28, 0);
        const float sideX = owner ? ReadOr<float>(owner + 0x3184, 0.0f) : 0.0f;
        const float sideZ = owner ? ReadOr<float>(owner + 0x3188, 0.0f) : 0.0f;
        const uint8_t flags7358 = owner ? ReadOr<uint8_t>(owner + 0x7358, 0) : 0;
        std::ostringstream oss;
        oss << "DismountSideClassify result=" << static_cast<int>(result)
            << " runtime=" << VehicleSeatTrace::Hex(runtime)
            << " kind=" << kind
            << " owner=" << VehicleSeatTrace::Hex(owner)
            << " sideX=" << sideX
            << " sideZ=" << sideZ
            << " flags7358=" << static_cast<int>(flags7358)
            << " objectTag=" << VehicleSeatTrace::Hex(objectTag)
            << " tagRange=" << VehicleSeatTrace::Hex(tagRange);
        g_logger->Log(oss.str());
    }
    return result;
}

bool InstallRideOffPoseVariantHook()
{
    const uintptr_t target =
        reinterpret_cast<uintptr_t>(g_module) + kRideOffPoseVariantRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kRideOffPoseVariantPatchLen);
    if (!trampoline)
        return false;
    g_originalRideOffPoseVariant = reinterpret_cast<RideOffPoseVariantFn>(trampoline);
    return JumpHook::WriteEntryJump(
        target, reinterpret_cast<void*>(&HookRideOffPoseVariant),
        kRideOffPoseVariantPatchLen);
}

bool InstallDismountSideClassifyHook()
{
    const uintptr_t target =
        reinterpret_cast<uintptr_t>(g_module) + kDismountSideClassifyRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kDismountSideClassifyPatchLen);
    if (!trampoline)
        return false;
    g_originalDismountSideClassify =
        reinterpret_cast<DismountSideClassifyFn>(trampoline);
    return JumpHook::WriteEntryJump(
        target, reinterpret_cast<void*>(&HookDismountSideClassify),
        kDismountSideClassifyPatchLen);
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;

    if (!InstallRideOffPoseVariantHook()) {
        logger.Log("InstallRideOffPoseVariantHook failed");
        return false;
    }
    if (!InstallDismountSideClassifyHook()) {
        logger.Log("InstallDismountSideClassifyHook failed");
        return false;
    }
    return true;
}

} // namespace RideOffPoseTrace
