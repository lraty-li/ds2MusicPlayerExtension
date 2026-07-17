#include "pch.h"
#include "RideOnEnterInterceptor.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

namespace RideOnEnterInterceptor {
namespace {

constexpr const char* kProcessAttachSignature =
    "4C 8B DC 55 56 49 8D AB ? ? ? ? 48 81 EC ? ? ? ? "
    "48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B 81";
constexpr size_t kProcessAttachPatchLen = 12;
constexpr const char* kDriveEnterSignature =
    "40 57 41 56 48 83 EC ? 48 8B 81 ? ? ? ? 48 8B F9 48 8B 88";
constexpr size_t kDriveEnterPatchLen = 15;
constexpr uint32_t kBoardingCompleteBit = 0x01000000;
constexpr uint64_t kSuppressionWindowMs = 5000;

using ProcessAttachFn = void(__fastcall*)(uintptr_t rideOn);
using DriveEnterFn = int64_t(__fastcall*)(uintptr_t driveState, uintptr_t a2, uintptr_t a3);

std::atomic<bool> g_started{false};
std::atomic<int> g_logBudget{32};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
ProcessAttachFn g_originalProcessAttach = nullptr;
DriveEnterFn g_originalDriveEnter = nullptr;
std::atomic<uintptr_t> g_activeBoardingRideOn{0};
std::atomic<uint64_t> g_fastSuppressionUntil{0};

uintptr_t FindPattern(const char* sig)
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize))
        return 0;
    return PatternScan::FindUnique(textStart, textSize, sig);
}

void LogLimited(const std::string& message)
{
    if (g_logBudget.fetch_sub(1) > 0)
        g_logger->Log(message);
}

void __fastcall HookProcessAttach(uintptr_t rideOn)
{
    uintptr_t plugin = 0;
    VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin);

    VehicleSeatTrace::Snapshot before = {};
    const bool haveBefore = VehicleSeatTrace::CaptureSnapshot(plugin, before);
    uintptr_t owner = 0;
    uint32_t ownerFlagsBefore = 0;
    VehicleSeatTrace::ReadValue(rideOn + 0xA0, owner);
    if (owner)
        VehicleSeatTrace::ReadValue(owner + 0x7378, ownerFlagsBefore);

    const bool forceCompletionGate = haveBefore && owner &&
        before.current == 1 && before.next == 1 && before.stage == 2 &&
        before.b18A && !before.b18B && before.b191;
    if (forceCompletionGate) {
        const uint32_t completedFlags =
            ownerFlagsBefore | kBoardingCompleteBit;
        if (VehicleSeatTrace::WriteValue(owner + 0x7378, completedFlags)) {
            g_activeBoardingRideOn.store(rideOn);
            g_fastSuppressionUntil.store(
                GetTickCount64() + kSuppressionWindowMs);
            std::ostringstream oss;
            oss << "FastBoarding native completion gate requested"
                << " owner7378=0x" << std::hex << ownerFlagsBefore
                << "->0x" << completedFlags << std::dec
                << VehicleSeatTrace::FormatSnapshot(plugin, before);
            LogLimited(oss.str());
        }
    }

    g_originalProcessAttach(rideOn);

    VehicleSeatTrace::Snapshot after = {};
    if (!VehicleSeatTrace::CaptureSnapshot(plugin, after))
        return;

    if (after.current == 1 && after.next == 1 && after.stage == 2 &&
        g_activeBoardingRideOn.exchange(rideOn) != rideOn) {
        std::ostringstream oss;
        oss << "BoardingCompletionScope publish"
            << VehicleSeatTrace::FormatSnapshot(plugin, after);
        LogLimited(oss.str());
    }

    if (haveBefore &&
        (before.stage != after.stage || before.b18B != after.b18B ||
         before.b18A != after.b18A || before.b189 != after.b189)) {
        uint32_t ownerFlagsAfter = 0;
        if (owner)
            VehicleSeatTrace::ReadValue(owner + 0x7378, ownerFlagsAfter);
        std::ostringstream oss;
        oss << "ProcessAttach original"
            << " stage " << before.stage << "->" << after.stage
            << " b18B " << static_cast<int>(before.b18B)
            << "->" << static_cast<int>(after.b18B)
            << " owner7378=0x" << std::hex << ownerFlagsBefore
            << "->0x" << ownerFlagsAfter << std::dec
            << VehicleSeatTrace::FormatSnapshot(plugin, after);
        LogLimited(oss.str());
    }
}

int64_t __fastcall HookDriveEnter(uintptr_t driveState, uintptr_t a2, uintptr_t a3)
{
    uintptr_t plugin = 0;
    VehicleSeatTrace::ReadValue(driveState + 0x88, plugin);
    uintptr_t rideOn = 0;
    VehicleSeatTrace::ReadValue(plugin + 0x150, rideOn);

    VehicleSeatTrace::Snapshot before = {};
    if (VehicleSeatTrace::CaptureSnapshot(plugin, before)) {
        std::ostringstream oss;
        oss << "DriveEnter entry" << VehicleSeatTrace::FormatSnapshot(plugin, before);
        LogLimited(oss.str());
    }

    const int64_t result = g_originalDriveEnter(driveState, a2, a3);
    if (g_fastSuppressionUntil.load() <= GetTickCount64())
        g_activeBoardingRideOn.store(0);

    VehicleSeatTrace::Snapshot after = {};
    if (VehicleSeatTrace::CaptureSnapshot(plugin, after)) {
        std::ostringstream oss;
        oss << "DriveEnter exit" << VehicleSeatTrace::FormatSnapshot(plugin, after);
        LogLimited(oss.str());
    }
    return result;
}

bool InstallHook(
    const char* signature, size_t patchLen, const char* name, void* hookFn, void** originalFn)
{
    const uintptr_t target = FindPattern(signature);
    if (!target) {
        g_logger->Log(std::string(name) + " signature not found");
        return false;
    }

    {
        std::ostringstream oss;
        oss << name << " resolved at " << VehicleSeatTrace::Hex(target);
        g_logger->Log(oss.str());
    }

    void* trampoline = JumpHook::MakeTrampoline(target, patchLen);
    if (!trampoline) {
        g_logger->Log(std::string(name) + " trampoline failed");
        return false;
    }

    *originalFn = trampoline;
    if (!JumpHook::WriteEntryJump(target, hookFn, patchLen)) {
        g_logger->Log(std::string(name) + " hook failed");
        return false;
    }
    return true;
}

} // namespace

uintptr_t ActiveBoardingRideOn()
{
    return g_activeBoardingRideOn.load();
}

bool FastBoardingSuppressionActive()
{
    return g_activeBoardingRideOn.load() != 0 &&
        g_fastSuppressionUntil.load() > GetTickCount64();
}

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;
    logger.Log("RideOnStateTrace v0.27.0: attach and drive boundaries");

    void* processAttachTrampoline = nullptr;
    if (!InstallHook(
            kProcessAttachSignature, kProcessAttachPatchLen, "ProcessAttach",
            reinterpret_cast<void*>(&HookProcessAttach), &processAttachTrampoline)) {
        return false;
    }
    g_originalProcessAttach = reinterpret_cast<ProcessAttachFn>(processAttachTrampoline);

    void* driveEnterTrampoline = nullptr;
    if (!InstallHook(
            kDriveEnterSignature, kDriveEnterPatchLen, "DriveEnter",
            reinterpret_cast<void*>(&HookDriveEnter), &driveEnterTrampoline)) {
        return false;
    }
    g_originalDriveEnter = reinterpret_cast<DriveEnterFn>(driveEnterTrampoline);

    logger.Log("RideOnStateTrace hooks installed");
    return true;
}

} // namespace RideOnEnterInterceptor
