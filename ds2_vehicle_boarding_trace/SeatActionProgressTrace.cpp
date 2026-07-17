#include "pch.h"
#include "SeatActionProgressTrace.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

namespace SeatActionProgressTrace {
namespace {

constexpr const char* kPoseRequestSignature =
    "40 56 48 83 EC ? 48 8B 81 ? ? ? ? 48 8B F1 48 8B 90";
constexpr const char* kProgressGateSignature =
    "83 B9 ? ? ? ? ? 74 ? 48 8B 81 ? ? ? ? 48 85 C0 74 ? "
    "83 B9 ? ? ? ? ? 75 ? C5 FA 10 40 ? C5 F8 2F 05 ? ? ? ? "
    "76 ? B0 ? C3 32 C0 C3 CC CC CC CC CC CC CC CC CC CC CC CC CC "
    "48 8B 89";
constexpr size_t kPoseRequestPatchLen = 13;
constexpr size_t kProgressGatePatchLen = 7;

using PoseRequestFn = uint8_t(__fastcall*)(uintptr_t rideOn);
using ProgressGateFn = bool(__fastcall*)(uintptr_t seatAction);

struct FrameTrace {
    uintptr_t rideOn = 0;
    uintptr_t seatAction = 0;
    uint32_t totalClips = 0;
    uint32_t transitionState = 0;
    uint32_t transitionActive = 0;
    uint32_t transitionFlags = 0;
    uint32_t approachCurrent = 0;
    uint32_t approachRequested = 0;
    uint32_t ownerActionFlags = 0;
    uintptr_t progressOwner = 0;
    float progress = 0.0f;
    uint16_t clipTypes[8] = {};
    uint32_t clipTypesRead = 0;
    bool gateCalled = false;
    bool gateResult = false;
};

std::atomic<bool> g_started{false};
std::atomic<int> g_logBudget{72};
std::atomic<uint32_t> g_poseCallCount{0};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
PoseRequestFn g_originalPoseRequest = nullptr;
ProgressGateFn g_originalProgressGate = nullptr;

thread_local bool t_inPoseRequest = false;
thread_local FrameTrace t_frame = {};

uintptr_t FindPattern(const char* signature)
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize))
        return 0;
    return PatternScan::FindUnique(textStart, textSize, signature);
}

void ReadSeatActionFields(FrameTrace& frame)
{
    if (!frame.seatAction)
        return;
    VehicleSeatTrace::ReadValue(frame.seatAction + 0x4E0, frame.totalClips);
    VehicleSeatTrace::ReadValue(frame.seatAction + 0x4E4, frame.transitionState);
    VehicleSeatTrace::ReadValue(frame.seatAction + 0x4EC, frame.transitionFlags);
    VehicleSeatTrace::ReadValue(frame.seatAction + 0x4FC, frame.transitionActive);
    VehicleSeatTrace::ReadValue(frame.seatAction + 0x1310, frame.approachCurrent);
    VehicleSeatTrace::ReadValue(frame.seatAction + 0x1314, frame.approachRequested);
    VehicleSeatTrace::ReadValue(frame.seatAction + 0x5C0, frame.progressOwner);
    if (frame.progressOwner)
        VehicleSeatTrace::ReadValue(frame.progressOwner + 0x14, frame.progress);

    uintptr_t clipOwner = 0;
    uintptr_t clipTypes = 0;
    if (VehicleSeatTrace::ReadValue(frame.seatAction + 0x340, clipOwner) && clipOwner &&
        VehicleSeatTrace::ReadValue(clipOwner + 0xA0, clipTypes) && clipTypes) {
        frame.clipTypesRead = frame.totalClips < 8 ? frame.totalClips : 8;
        for (uint32_t i = 0; i < frame.clipTypesRead; ++i)
            VehicleSeatTrace::ReadValue(clipTypes + sizeof(uint16_t) * i, frame.clipTypes[i]);
    }
}

bool __fastcall HookProgressGate(uintptr_t seatAction)
{
    const bool result = g_originalProgressGate(seatAction);
    if (t_inPoseRequest) {
        t_frame.seatAction = seatAction;
        t_frame.gateCalled = true;
        t_frame.gateResult = result;
        ReadSeatActionFields(t_frame);
    }
    return result;
}

void LogFrame(const FrameTrace& frame, uint16_t poseId, uint8_t poseActive)
{
    if (g_logBudget.fetch_sub(1) <= 0)
        return;
    std::ostringstream oss;
    oss << "SeatActionFrame rideOn=" << VehicleSeatTrace::Hex(frame.rideOn)
        << " seatAction=" << VehicleSeatTrace::Hex(frame.seatAction)
        << " mode=" << frame.transitionState
        << " active=" << frame.transitionActive
        << " flags=0x" << std::hex << frame.transitionFlags << std::dec
        << " approach=" << frame.approachCurrent << "->" << frame.approachRequested
        << " owner7378=0x" << std::hex << frame.ownerActionFlags << std::dec
        << " clips=" << frame.totalClips
        << " progressOwner=" << VehicleSeatTrace::Hex(frame.progressOwner)
        << " progress=" << frame.progress
        << " gate=" << (frame.gateCalled ? static_cast<int>(frame.gateResult) : -1)
        << " poseId=" << poseId
        << " poseActive=" << static_cast<int>(poseActive);
    if (frame.clipTypesRead) {
        oss << " actualTypes=";
        for (uint32_t i = 0; i < frame.clipTypesRead; ++i) {
            if (i)
                oss << ',';
            oss << frame.clipTypes[i];
        }
    }
    g_logger->Log(oss.str());
}

uint8_t __fastcall HookPoseRequest(uintptr_t rideOn)
{
    t_frame = {};
    t_frame.rideOn = rideOn;
    t_inPoseRequest = true;
    const uint8_t result = g_originalPoseRequest(rideOn);
    t_inPoseRequest = false;
    ReadSeatActionFields(t_frame);

    uint16_t poseId = 0;
    uint8_t poseActive = 0;
    uintptr_t owner = 0;
    uintptr_t poseOwner = 0;
    if (VehicleSeatTrace::ReadValue(rideOn + 0xA0, owner) && owner &&
        VehicleSeatTrace::ReadValue(owner + 0x7558, poseOwner) && poseOwner) {
        VehicleSeatTrace::ReadValue(owner + 0x7378, t_frame.ownerActionFlags);
        VehicleSeatTrace::ReadValue(poseOwner + 0x788, poseId);
        VehicleSeatTrace::ReadValue(poseOwner + 0x2104, poseActive);
    }

    const uint32_t call = g_poseCallCount.fetch_add(1);
    if (call < 4 || call % 12 == 0 || t_frame.gateResult)
        LogFrame(t_frame, poseId, poseActive);
    return result;
}

bool InstallHook(
    const char* signature, size_t patchLen, const char* name,
    void* hook, void** original)
{
    const uintptr_t target = FindPattern(signature);
    if (!target) {
        g_logger->Log(std::string(name) + " signature not found");
        return false;
    }
    void* trampoline = JumpHook::MakeTrampoline(target, patchLen);
    if (!trampoline)
        return false;
    *original = trampoline;
    if (!JumpHook::WriteEntryJump(target, hook, patchLen))
        return false;
    g_logger->Log(std::string(name) + " resolved at " + VehicleSeatTrace::Hex(target));
    return true;
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;
    g_module = gameModule;
    g_logger = &logger;

    if (!FindPattern(kPoseRequestSignature) || !FindPattern(kProgressGateSignature)) {
        logger.Log("SeatActionProgressTrace signature preflight failed");
        return false;
    }

    void* original = nullptr;
    if (!InstallHook(kPoseRequestSignature, kPoseRequestPatchLen,
            "RideOnPoseRequest", reinterpret_cast<void*>(&HookPoseRequest), &original))
        return false;
    g_originalPoseRequest = reinterpret_cast<PoseRequestFn>(original);
    if (!InstallHook(kProgressGateSignature, kProgressGatePatchLen,
            "SeatActionProgressGate", reinterpret_cast<void*>(&HookProgressGate), &original))
        return false;
    g_originalProgressGate = reinterpret_cast<ProgressGateFn>(original);
    logger.Log("SeatActionProgressTrace read-only hooks installed");
    return true;
}

} // namespace SeatActionProgressTrace
