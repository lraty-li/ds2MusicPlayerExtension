#include "pch.h"
#include "BoardingCompletionGate.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "RideOnEnterInterceptor.h"
#include "SeatTransitionTrace.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace BoardingCompletionGate {
namespace {

constexpr const char* kActionParamBoolSignature =
    "48 89 6C 24 ? 56 48 83 EC ? 48 8B 01 8B EA 48 8B F1 FF 90 ? ? ? ? "
    "3D ? ? ? ? 75 ? 32 C0 48 8B 6C 24 ? 48 83 C4 ? 5E C3 "
    "48 89 5C 24 ? 8B D5 48 89 7C 24 ? 48 8B CE 48 8B BE ? ? ? ? "
    "48 8B 07 48 8B 98 ? ? ? ? 48 8B 06 FF 90 ? ? ? ? 45 33 C0 "
    "48 8B CF 8B D0 48 8B C3 48 8B 7C 24 ? 48 8B 5C 24 ? "
    "48 8B 6C 24 ? 48 83 C4 ? 5E 48 FF E0 "
    "CC CC CC CC CC CC CC CC CC 40 53";
constexpr size_t kPatchLen = 15;
constexpr uint32_t kBoardingCompleteEvent = 0xED;

using ActionParamBoolFn = uint8_t(__fastcall*)(uintptr_t params, uint32_t paramId);

std::atomic<bool> g_started{false};
std::atomic<bool> g_loggedNativeCompletion{false};
std::atomic<bool> g_loggedGraphOwner{false};
std::atomic<uint32_t> g_lastClipSampleBucket{UINT32_MAX};
std::atomic<int> g_logBudget{12};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
ActionParamBoolFn g_original = nullptr;

void LogGraphOwner(uintptr_t params)
{
    if (g_loggedGraphOwner.exchange(true))
        return;

    uintptr_t manager = 0;
    uintptr_t graphInstance = 0;
    uintptr_t descriptorOwner = 0;
    uintptr_t descriptorTable = 0;
    uintptr_t evaluator = 0;
    uintptr_t stateOwner = 0;
    uintptr_t stateData = 0;
    uintptr_t bindingOwner = 0;
    uintptr_t bindingData = 0;
    uintptr_t argState = 0;
    uintptr_t argOutput = 0;
    uint32_t descriptorCount = 0;
    uint32_t evaluationDepth = 0;

    VehicleSeatTrace::ReadValue(params + 0x8A8, manager);
    if (manager)
        VehicleSeatTrace::ReadValue(manager + 0xB8, graphInstance);
    if (graphInstance) {
        VehicleSeatTrace::ReadValue(graphInstance + 0x20, descriptorOwner);
        VehicleSeatTrace::ReadValue(graphInstance + 0x30, stateOwner);
        VehicleSeatTrace::ReadValue(graphInstance + 0x40, bindingOwner);
        VehicleSeatTrace::ReadValue(graphInstance + 0x78, argState);
        VehicleSeatTrace::ReadValue(graphInstance + 0xC0, argOutput);
        VehicleSeatTrace::ReadValue(graphInstance + 0x108, evaluationDepth);
    }
    if (descriptorOwner) {
        VehicleSeatTrace::ReadValue(descriptorOwner + 0x20, descriptorCount);
        VehicleSeatTrace::ReadValue(descriptorOwner + 0x28, descriptorTable);
    }
    if (descriptorCount && descriptorTable)
        VehicleSeatTrace::ReadValue(descriptorTable + 0xD8, evaluator);
    if (stateOwner)
        VehicleSeatTrace::ReadValue(stateOwner + 0x8, stateData);
    if (bindingOwner)
        VehicleSeatTrace::ReadValue(bindingOwner + 0x8, bindingData);

    HMODULE evaluatorModule = nullptr;
    char evaluatorModulePath[MAX_PATH] = {};
    MEMORY_BASIC_INFORMATION evaluatorMemory = {};
    if (evaluator) {
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(evaluator), &evaluatorModule);
        if (evaluatorModule)
            GetModuleFileNameA(evaluatorModule, evaluatorModulePath, MAX_PATH);
        VirtualQuery(reinterpret_cast<void*>(evaluator),
            &evaluatorMemory, sizeof(evaluatorMemory));
    }

    std::ostringstream oss;
    oss << "AnimationGraphOwner params=" << VehicleSeatTrace::Hex(params)
        << " manager=" << VehicleSeatTrace::Hex(manager)
        << " instance=" << VehicleSeatTrace::Hex(graphInstance)
        << " descriptors=" << descriptorCount
        << " descriptorOwner=" << VehicleSeatTrace::Hex(descriptorOwner)
        << " descriptorTable=" << VehicleSeatTrace::Hex(descriptorTable)
        << " evaluator=" << VehicleSeatTrace::Hex(evaluator)
        << " evaluatorModule="
        << VehicleSeatTrace::Hex(reinterpret_cast<uintptr_t>(evaluatorModule))
        << " evaluatorModuleRva=" << VehicleSeatTrace::Hex(
            evaluatorModule ? evaluator - reinterpret_cast<uintptr_t>(evaluatorModule) : 0)
        << " evaluatorModulePath="
        << (evaluatorModulePath[0] ? evaluatorModulePath : "<dynamic>")
        << " allocationBase=" << VehicleSeatTrace::Hex(
            reinterpret_cast<uintptr_t>(evaluatorMemory.AllocationBase))
        << " regionSize=" << evaluatorMemory.RegionSize
        << " protect=0x" << std::hex << evaluatorMemory.Protect
        << " type=0x" << evaluatorMemory.Type << std::dec
        << " stateOwner=" << VehicleSeatTrace::Hex(stateOwner)
        << " stateData=" << VehicleSeatTrace::Hex(stateData)
        << " bindingOwner=" << VehicleSeatTrace::Hex(bindingOwner)
        << " bindingData=" << VehicleSeatTrace::Hex(bindingData)
        << " argState=" << VehicleSeatTrace::Hex(argState)
        << " argOutput=" << VehicleSeatTrace::Hex(argOutput)
        << " evaluationDepth=" << evaluationDepth;
    g_logger->Log(oss.str());
}

void LogSeatClipSample(float elapsed, uintptr_t params)
{
    const uint32_t bucket = static_cast<uint32_t>(elapsed * 4.0f);
    if (g_lastClipSampleBucket.exchange(bucket) == bucket)
        return;

    const uintptr_t controller = SeatTransitionTrace::ActiveSeatController();
    if (!controller)
        return;
    uintptr_t clipOwner = 0;
    uintptr_t clipTypes = 0;
    uint32_t clipCount = 0;
    uint32_t mode = 0;
    uint32_t flags = 0;
    uint32_t active = 0;
    float value78 = 0.0f;
    uintptr_t manager = 0;
    uintptr_t graphInstance = 0;
    uintptr_t graphOutput = 0;
    uintptr_t resultCountOwner = 0;
    uintptr_t resultItems = 0;
    uintptr_t resultAltItems = 0;
    uintptr_t resultSingle = 0;
    uintptr_t firstResultItem = 0;
    uint32_t resultCount = 0;
    VehicleSeatTrace::ReadValue(controller + 0x340, clipOwner);
    VehicleSeatTrace::ReadValue(controller + 0x4E0, clipCount);
    VehicleSeatTrace::ReadValue(controller + 0x4E4, mode);
    VehicleSeatTrace::ReadValue(controller + 0x4EC, flags);
    VehicleSeatTrace::ReadValue(controller + 0x4FC, active);
    if (clipOwner) {
        VehicleSeatTrace::ReadValue(clipOwner + 0x78, value78);
        VehicleSeatTrace::ReadValue(clipOwner + 0xA0, clipTypes);
    }
    VehicleSeatTrace::ReadValue(params + 0x8A8, manager);
    if (manager)
        VehicleSeatTrace::ReadValue(manager + 0xB8, graphInstance);
    if (graphInstance)
        VehicleSeatTrace::ReadValue(graphInstance + 0xC0, graphOutput);
    if (graphOutput) {
        VehicleSeatTrace::ReadValue(graphOutput, resultCountOwner);
        VehicleSeatTrace::ReadValue(graphOutput + 0x10, resultItems);
        VehicleSeatTrace::ReadValue(graphOutput + 0x20, resultAltItems);
        VehicleSeatTrace::ReadValue(graphOutput + 0x40, resultSingle);
    }
    if (resultCountOwner)
        VehicleSeatTrace::ReadValue(resultCountOwner, resultCount);
    if (resultCount && resultItems)
        VehicleSeatTrace::ReadValue(resultItems, firstResultItem);

    std::ostringstream oss;
    oss << "SeatClipSample elapsed=" << elapsed
        << " controller=" << VehicleSeatTrace::Hex(controller)
        << " clipOwner=" << VehicleSeatTrace::Hex(clipOwner)
        << " count=" << clipCount
        << " mode=" << mode
        << " flags=0x" << std::hex << flags << std::dec
        << " active=" << active
        << " value78=" << value78
        << " clipTypes=" << VehicleSeatTrace::Hex(clipTypes)
        << " graphOutput=" << VehicleSeatTrace::Hex(graphOutput)
        << " resultCount=" << resultCount
        << " resultItems=" << VehicleSeatTrace::Hex(resultItems)
        << " resultAltItems=" << VehicleSeatTrace::Hex(resultAltItems)
        << " resultSingle=" << VehicleSeatTrace::Hex(resultSingle)
        << " firstResultItem=" << VehicleSeatTrace::Hex(firstResultItem);
    const uint32_t readCount = clipCount < 8 ? clipCount : 8;
    for (uint32_t i = 0; clipOwner && i < readCount; ++i) {
        uintptr_t handle = 0;
        uint16_t type = 0;
        VehicleSeatTrace::ReadValue(clipOwner + 0x440 + sizeof(handle) * i, handle);
        if (clipTypes)
            VehicleSeatTrace::ReadValue(clipTypes + sizeof(type) * i, type);
        oss << " clip" << i << "=" << VehicleSeatTrace::Hex(handle)
            << "/type" << type;
    }
    g_logger->Log(oss.str());
}

uint8_t __fastcall HookActionParamBool(uintptr_t params, uint32_t paramId)
{
    const uintptr_t rideOn = RideOnEnterInterceptor::ActiveBoardingRideOn();
    const bool isCompletionQuery = paramId == kBoardingCompleteEvent;
    if (isCompletionQuery && g_logBudget.fetch_sub(1) > 0) {
        g_logger->Log("boarding completion query enter params=" +
            VehicleSeatTrace::Hex(params) +
            " activeRideOn=" + VehicleSeatTrace::Hex(rideOn));
    }

    const uint8_t nativeResult = g_original(params, paramId);
    VehicleSeatTrace::Snapshot snapshot = {};
    uintptr_t plugin = 0;
    const bool traceBoardingQuery = isCompletionQuery && rideOn &&
        VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin) &&
        VehicleSeatTrace::CaptureSnapshot(plugin, snapshot) &&
        snapshot.rideOn == rideOn && snapshot.current == 1 &&
        snapshot.next == 1 && snapshot.stage == 2;
    if (isCompletionQuery && g_logBudget.fetch_sub(1) > 0) {
        std::ostringstream oss;
        oss << "boarding completion query native="
            << static_cast<int>(nativeResult)
            << " eligible=" << static_cast<int>(traceBoardingQuery);
        g_logger->Log(oss.str());
    }
    if (traceBoardingQuery)
        LogGraphOwner(params);
    if (traceBoardingQuery)
        LogSeatClipSample(snapshot.elapsed, params);
    if (traceBoardingQuery && nativeResult &&
        !g_loggedNativeCompletion.exchange(true)) {
        float elapsed = 0.0f;
        VehicleSeatTrace::ReadValue(rideOn + 0x180, elapsed);
        std::ostringstream oss;
        oss << "native boarding completion event"
            << " rideOn=" << VehicleSeatTrace::Hex(rideOn)
            << " params=" << VehicleSeatTrace::Hex(params)
            << " elapsed=" << elapsed;
        g_logger->Log(oss.str());
    }
    return nativeResult;
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;
    g_module = gameModule;
    g_logger = &logger;

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(gameModule, ".text", textStart, textSize))
        return false;
    const uintptr_t target =
        PatternScan::FindUnique(textStart, textSize, kActionParamBoolSignature);
    if (!target) {
        logger.Log("BoardingCompletionGate signature not found or not unique");
        return false;
    }

    void* trampoline = JumpHook::MakeTrampoline(target, kPatchLen);
    if (!trampoline)
        return false;
    g_original = reinterpret_cast<ActionParamBoolFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookActionParamBool), kPatchLen)) {
        return false;
    }

    logger.Log("BoardingCompletionGate trace-only installed at " +
        VehicleSeatTrace::Hex(target));
    return true;
}

} // namespace BoardingCompletionGate
