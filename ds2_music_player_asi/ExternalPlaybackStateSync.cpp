#include "pch.h"

#include "ExternalPlaybackStateSync.h"

#include "ExternalPlaybackStatePolicy.h"
#include "GameLayout.h"
#include "HookUtils.h"
#include "PatternScan.h"
#include "SpecialTrackIds.h"

#include <atomic>
#include <sstream>

namespace
{
constexpr const char* kManualPausePattern =
    "48 89 5C 24 ?? 57 48 83 EC 20 48 8B F9 "
    "48 8B 89 ?? ?? ?? ?? 48 85 C9 0F 85";
constexpr const char* kManualResumePattern =
    "40 57 48 83 EC 20 0F B6 81 ?? ?? ?? ?? 48 8B F9 3C 03";

using ReadPlaybackStateFn = int(__cdecl*)(uint32_t*, int*, int*);
using ApplyStateFn = void(__fastcall*)(void*);

struct RuntimeSnapshot
{
    uint8_t playState = 0;
    uint16_t autoBlockMask = 0;
    void* currentRuntime = nullptr;
    uint32_t trackId = 0;
};

Logger* g_logger = nullptr;
ReadPlaybackStateFn g_readPlaybackState = nullptr;
ApplyStateFn g_applyPause = nullptr;
ApplyStateFn g_applyResume = nullptr;
std::atomic<void*> g_runtime{nullptr};
std::atomic<uint32_t> g_observedVersion{0};
std::atomic<uint64_t> g_pendingSnapshot{0};
std::atomic<uint32_t> g_appliedVersion{0};
std::atomic<uint32_t> g_deferredLoggedVersion{0};
std::atomic<bool> g_applying{false};

void Log(const std::string& text)
{
    if (g_logger) g_logger->Log(text);
}

ReadPlaybackStateFn ResolveReader()
{
    if (g_readPlaybackState) return g_readPlaybackState;
    HMODULE module = GetModuleHandleW(L"ds2_dll_music_resource.dll");
    if (!module) return nullptr;
    auto* proc = GetProcAddress(module, "DS2AudioStreamReadPlaybackState");
    g_readPlaybackState = reinterpret_cast<ReadPlaybackStateFn>(proc);
    return g_readPlaybackState;
}

bool ReadSnapshot(void* runtime, RuntimeSnapshot& snapshot)
{
    __try
    {
        auto* bytes = static_cast<uint8_t*>(runtime);
        snapshot.playState =
            *reinterpret_cast<uint8_t*>(
                bytes + GameLayout::MusicRuntime::kPlayState);
        snapshot.autoBlockMask =
            *reinterpret_cast<uint16_t*>(
                bytes + GameLayout::MusicRuntime::kAutoBlockMask);
        snapshot.currentRuntime =
            *reinterpret_cast<void**>(
                bytes + GameLayout::MusicRuntime::kCurrentRuntime);
        snapshot.trackId =
            *reinterpret_cast<uint32_t*>(
                bytes + GameLayout::MusicRuntime::kCurrentTrackId);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void LogObserved(uint32_t version, int state)
{
    std::ostringstream oss;
    oss << "external playback state observed version=" << version
        << " state=" << (state < 0 ? "unknown" : state ? "paused" : "playing");
    Log(oss.str());
}

void MarkApplied(uint32_t version, const RuntimeSnapshot& snapshot)
{
    g_appliedVersion.store(version, std::memory_order_release);
    std::ostringstream oss;
    oss << "external playback state accepted version=" << version
        << " no_action state=" << int(snapshot.playState)
        << " trackId=" << HookUtils::HexU64(snapshot.trackId)
        << " blockMask=" << HookUtils::HexU64(snapshot.autoBlockMask);
    Log(oss.str());
}

void ApplyAction(uint32_t version, int state, void* runtime,
    const RuntimeSnapshot& before,
    ExternalPlaybackStatePolicy::Decision decision)
{
    const char* action =
        decision == ExternalPlaybackStatePolicy::Decision::Pause ?
        "pause" : "resume";
    g_applying.store(true, std::memory_order_release);
    if (decision == ExternalPlaybackStatePolicy::Decision::Pause)
    {
        g_applyPause(runtime);
    }
    else
    {
        g_applyResume(runtime);
    }
    g_applying.store(false, std::memory_order_release);
    g_appliedVersion.store(version, std::memory_order_release);

    RuntimeSnapshot after;
    ReadSnapshot(runtime, after);
    std::ostringstream oss;
    oss << "external playback state applied version=" << version
        << " desired=" << (state ? "paused" : "playing")
        << " action=" << action
        << " state=" << int(before.playState)
        << "->" << int(after.playState);
    Log(oss.str());
}
}

namespace ExternalPlaybackStateSync
{
bool Configure(HMODULE gameModule, const Logger& logger)
{
    g_logger = const_cast<Logger*>(&logger);
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(gameModule, ".text", textStart, textSize))
    {
        Log("external playback sync failed: .text not found");
        return false;
    }
    const uintptr_t pause =
        PatternScan::Find(textStart, textSize, kManualPausePattern);
    const uintptr_t resume =
        PatternScan::Find(textStart, textSize, kManualResumePattern);
    if (!pause || !resume)
    {
        Log("external playback sync failed: control pattern not found");
        return false;
    }
    g_applyPause = reinterpret_cast<ApplyStateFn>(pause);
    g_applyResume = reinterpret_cast<ApplyStateFn>(resume);
    Log("external playback sync configured pauseRva=" +
        HookUtils::HexU64(pause - reinterpret_cast<uintptr_t>(gameModule)) +
        " resumeRva=" +
        HookUtils::HexU64(resume - reinterpret_cast<uintptr_t>(gameModule)));
    return true;
}

void Reset()
{
    g_runtime.store(nullptr);
    g_readPlaybackState = nullptr;
    g_observedVersion.store(0);
    g_pendingSnapshot.store(0);
    g_appliedVersion.store(0);
    g_deferredLoggedVersion.store(0);
    g_applying.store(false);
}

void SetRuntime(void* runtime)
{
    if (runtime) g_runtime.store(runtime, std::memory_order_release);
}

bool Poll()
{
    auto readState = ResolveReader();
    if (!readState) return false;
    uint32_t version = 0;
    int known = 0;
    int paused = 0;
    if (!readState(&version, &known, &paused) || !version) return false;
    if (version != g_observedVersion.load(std::memory_order_acquire))
    {
        const int state = known ? (paused ? 1 : 0) : -1;
        const uint64_t snapshot =
            (static_cast<uint64_t>(version) << 32) |
            static_cast<uint32_t>(state + 1);
        g_pendingSnapshot.store(snapshot, std::memory_order_release);
        g_observedVersion.store(version, std::memory_order_release);
        LogObserved(version, state);
    }
    return static_cast<uint32_t>(
        g_pendingSnapshot.load(std::memory_order_acquire) >> 32) !=
        g_appliedVersion.load(std::memory_order_acquire);
}

void ApplyPendingOnGameThread()
{
    if (g_applying.load(std::memory_order_acquire)) return;
    const uint64_t pending =
        g_pendingSnapshot.load(std::memory_order_acquire);
    const uint32_t version = static_cast<uint32_t>(pending >> 32);
    if (!version ||
        version == g_appliedVersion.load(std::memory_order_acquire))
    {
        return;
    }
    void* runtime = g_runtime.load(std::memory_order_acquire);
    if (!runtime) return;

    RuntimeSnapshot snapshot;
    if (!ReadSnapshot(runtime, snapshot))
    {
        g_runtime.store(nullptr);
        return;
    }
    const int state = static_cast<int>(
        static_cast<uint32_t>(pending)) - 1;
    const ExternalPlaybackStatePolicy::Input input = {
        snapshot.trackId == SpecialTrackIds::kCustomTrackId,
        state >= 0,
        state > 0,
        snapshot.playState,
        snapshot.currentRuntime != nullptr,
        snapshot.autoBlockMask,
    };
    const auto decision = ExternalPlaybackStatePolicy::Decide(input);
    if (decision == ExternalPlaybackStatePolicy::Decision::MarkApplied)
    {
        MarkApplied(version, snapshot);
        return;
    }
    if (decision == ExternalPlaybackStatePolicy::Decision::Defer)
    {
        if (g_deferredLoggedVersion.exchange(version) != version)
        {
            Log("external playback state deferred version=" +
                std::to_string(version) +
                " state=" + std::to_string(snapshot.playState));
        }
        return;
    }
    ApplyAction(version, state, runtime, snapshot, decision);
}

bool IsApplying()
{
    return g_applying.load(std::memory_order_acquire);
}
}
