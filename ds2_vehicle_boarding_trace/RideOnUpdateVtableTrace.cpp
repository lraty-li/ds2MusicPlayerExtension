#include "pch.h"
#include "RideOnUpdateVtableTrace.h"

#include "FastBoardingSession.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>

namespace RideOnUpdateVtableTrace {
namespace {

constexpr const char* kUpdateSignature =
    "40 53 56 57 48 83 EC ? C5 F2 58 81";
constexpr char kExpectedTypeName[] = ".?AVDSPlayerVehicleRideOnState@@";

using UpdateFn = void(__fastcall*)(uintptr_t rideOn, float delta, float a3);

constexpr size_t kPluginBytes = 0x400;
constexpr size_t kRideOnBytes = 0x200;

std::atomic<bool> g_loggedFirst{false};
const Logger* g_logger = nullptr;
UpdateFn g_original = nullptr;
std::mutex g_deltaMutex;
std::array<uint8_t, kPluginBytes> g_previousPlugin{};
std::array<uint8_t, kRideOnBytes> g_previousRideOn{};
bool g_havePrevious = false;

template <size_t Size>
bool ReadBytes(uintptr_t address, std::array<uint8_t, Size>& bytes)
{
    __try {
        memcpy(bytes.data(), reinterpret_cast<const void*>(address), Size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <size_t Size>
void AppendDelta(
    std::ostringstream& oss, const char* label,
    const std::array<uint8_t, Size>& previous,
    const std::array<uint8_t, Size>& current)
{
    size_t count = 0;
    oss << ' ' << label << "Delta=";
    for (size_t i = 0; i < Size; ++i) {
        if (previous[i] == current[i])
            continue;
        if (count < 96) {
            oss << (count ? "," : "") << "0x" << std::hex << i
                << ':' << static_cast<uint32_t>(previous[i])
                << "->" << static_cast<uint32_t>(current[i]) << std::dec;
        }
        ++count;
    }
    if (count > 96)
        oss << ",...";
    oss << " count=" << count;

    size_t wordCount = 0;
    oss << " words=";
    for (size_t i = 0; i + sizeof(uint32_t) <= Size;
         i += sizeof(uint32_t)) {
        if (memcmp(previous.data() + i, current.data() + i,
                sizeof(uint32_t)) == 0) {
            continue;
        }
        if (wordCount < 24) {
            uint32_t oldWord = 0;
            uint32_t newWord = 0;
            float oldFloat = 0.0f;
            float newFloat = 0.0f;
            memcpy(&oldWord, previous.data() + i, sizeof(oldWord));
            memcpy(&newWord, current.data() + i, sizeof(newWord));
            memcpy(&oldFloat, previous.data() + i, sizeof(oldFloat));
            memcpy(&newFloat, current.data() + i, sizeof(newFloat));
            oss << (wordCount ? "," : "") << "0x" << std::hex << i
                << ":0x" << oldWord << "->0x" << newWord << std::dec
                << "(" << oldFloat << "->" << newFloat << ')';
        }
        ++wordCount;
    }
    if (wordCount > 24)
        oss << ",...";
    oss << " wordCount=" << wordCount;
}

bool StateChanged(
    const VehicleSeatTrace::Snapshot& before,
    const VehicleSeatTrace::Snapshot& after)
{
    return before.current != after.current || before.next != after.next ||
        before.flag != after.flag || before.stage != after.stage ||
        before.b189 != after.b189 || before.b18A != after.b18A ||
        before.b18B != after.b18B || before.b190 != after.b190 ||
        before.b191 != after.b191 || before.b192 != after.b192 ||
        before.b381 != after.b381 || before.b3B1 != after.b3B1;
}

void __fastcall HookUpdate(uintptr_t rideOn, float delta, float a3)
{
    uintptr_t plugin = 0;
    VehicleSeatTrace::Snapshot before = {};
    VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin);
    const bool haveBefore = VehicleSeatTrace::CaptureSnapshot(plugin, before);
    std::array<uint8_t, kPluginBytes> currentPlugin{};
    std::array<uint8_t, kRideOnBytes> currentRideOn{};
    const bool haveCurrentBytes = plugin &&
        ReadBytes(plugin, currentPlugin) && ReadBytes(rideOn, currentRideOn);
    std::array<uint8_t, kPluginBytes> previousPlugin{};
    std::array<uint8_t, kRideOnBytes> previousRideOn{};
    bool havePrevious = false;
    {
        std::lock_guard<std::mutex> lock(g_deltaMutex);
        havePrevious = g_havePrevious;
        if (havePrevious) {
            previousPlugin = g_previousPlugin;
            previousRideOn = g_previousRideOn;
        }
    }
    const uintptr_t previousUpdateScope =
        FastBoardingSession::EnterRideOnUpdate(rideOn);
    g_original(rideOn, delta, a3);
    FastBoardingSession::LeaveRideOnUpdate(previousUpdateScope);
    VehicleSeatTrace::Snapshot after = {};
    const bool haveAfter = VehicleSeatTrace::CaptureSnapshot(plugin, after);
    const bool changed = haveBefore && haveAfter && StateChanged(before, after);
    if (plugin) {
        std::array<uint8_t, kPluginBytes> afterPlugin{};
        std::array<uint8_t, kRideOnBytes> afterRideOn{};
        if (ReadBytes(plugin, afterPlugin) && ReadBytes(rideOn, afterRideOn)) {
            std::lock_guard<std::mutex> lock(g_deltaMutex);
            g_previousPlugin = afterPlugin;
            g_previousRideOn = afterRideOn;
            g_havePrevious = true;
        }
    }
    if (g_loggedFirst.exchange(true) && !changed)
        return;

    std::ostringstream oss;
    oss << "RideOnUpdateVtable original"
        << " delta=" << delta << " a3=" << a3
        << " haveBefore=" << haveBefore
        << " haveAfter=" << haveAfter;
    if (haveBefore)
        oss << " before{" << VehicleSeatTrace::FormatSnapshot(plugin, before) << " }";
    if (haveAfter)
        oss << " after{" << VehicleSeatTrace::FormatSnapshot(plugin, after) << " }";
    if (changed && before.next != after.next &&
        havePrevious && haveCurrentBytes) {
        AppendDelta(oss, "plugin", previousPlugin, currentPlugin);
        AppendDelta(oss, "rideOn", previousRideOn, currentRideOn);
    }
    g_logger->Log(oss.str());
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    VtableLocator::Match match = {};
    if (!VtableLocator::FindUnique(gameModule, kUpdateSignature, match)) {
        logger.Log("RideOn Update vtable lookup failed pointerMatches=" +
            std::to_string(match.pointerMatches) + " rttiMatches=" +
            std::to_string(match.rttiMatches));
        return false;
    }
    std::ostringstream candidate;
    candidate << "RideOn Update vtable candidate"
        << " target=" << VehicleSeatTrace::Hex(match.target)
        << " vtable=" << VehicleSeatTrace::Hex(match.vtable)
        << " slot=" << match.slotIndex
        << " subobjectOffset=0x" << std::hex << match.subobjectOffset
        << std::dec << " type=" << match.typeName;
    logger.Log(candidate.str());
    if (match.typeName != kExpectedTypeName) {
        logger.Log("RideOn Update vtable type validation failed");
        return false;
    }

    g_logger = &logger;
    g_original = reinterpret_cast<UpdateFn>(match.target);
    if (!VtableLocator::SwapSlot(
            match.slot, match.target,
            reinterpret_cast<void*>(&HookUpdate))) {
        logger.Log("RideOn Update vtable observer install failed");
        return false;
    }
    logger.Log("RideOn Update vtable observer installed");
    return true;
}

} // namespace RideOnUpdateVtableTrace
