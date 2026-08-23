#include "pch.h"

#include "PlayStateMonitor.h"

#include "DynamicTrackTitleSync.h"
#include "ExternalPlaybackStateSync.h"
#include "GameLayout.h"
#include "HookUtils.h"
#include "PatternScan.h"
#include "RuntimeEntryTitleRefresh.h"
#include "SpecialTrackIds.h"

#include <intrin.h>
#include <sstream>

namespace
{
constexpr uint32_t kPatchBytes = 13;

const char* kSetPlayStatePattern =
    "40 57 48 83 EC 20 0F B6 81 10 19 00 00 48 8B F9 "
    "3A C2 0F 84 ?? ?? ?? ?? 3C 05 75 07 C6 81 B6 28 00 00 00";

using SetPlayStateFn = int64_t(__fastcall*)(void*, uint8_t);
using SendBrowserControlFn = int(__cdecl*)(const char*);

enum class BrowserPauseReason : uint8_t
{
    None,
    AutoBlock,
    Manual,
    External,
};

Logger* g_logger = nullptr;
SetPlayStateFn g_original = nullptr;
SendBrowserControlFn g_sendBrowserControl = nullptr;
BrowserPauseReason g_browserPauseReason = BrowserPauseReason::None;

void Log(const std::string& text)
{
    if (g_logger) g_logger->Log(text);
}

uint8_t ReadU8(void* base, uint32_t offset)
{
    __try { return *reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(base) + offset); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFF; }
}

uint32_t ReadU32(void* base, uint32_t offset)
{
    __try { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(base) + offset); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void* ReadPtr(void* base, uint32_t offset)
{
    __try { return *reinterpret_cast<void**>(static_cast<uint8_t*>(base) + offset); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

const char* StateName(uint8_t state)
{
    switch (state)
    {
    case 0: return "idle";
    case 1: return "playing";
    case 2: return "paused";
    case 3: return "state3";
    case 4: return "state4";
    case 5: return "state5";
    default: return "unknown";
    }
}

void WriteAbsoluteJump(uint8_t* target, void* destination)
{
    target[0] = 0x48;
    target[1] = 0xB8;
    *reinterpret_cast<void**>(target + 2) = destination;
    target[10] = 0xFF;
    target[11] = 0xE0;
}

bool InstallDetour(uintptr_t target, void* detour, void** original)
{
    auto* gateway = static_cast<uint8_t*>(VirtualAlloc(nullptr, kPatchBytes + 12,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!gateway) return false;

    memcpy(gateway, reinterpret_cast<void*>(target), kPatchBytes);
    WriteAbsoluteJump(gateway + kPatchBytes, reinterpret_cast<void*>(target + kPatchBytes));

    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(target), kPatchBytes,
        PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        return false;
    }

    auto* patch = reinterpret_cast<uint8_t*>(target);
    WriteAbsoluteJump(patch, detour);
    patch[12] = 0x90;
    VirtualProtect(patch, kPatchBytes, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), patch, kPatchBytes);
    *original = gateway;
    return true;
}

SendBrowserControlFn ResolveBrowserControl()
{
    if (g_sendBrowserControl) return g_sendBrowserControl;
    HMODULE module = GetModuleHandleW(L"ds2_dll_music_resource.dll");
    if (!module) return nullptr;
    auto* proc = GetProcAddress(module, "DS2AudioStreamSendBrowserControl");
    g_sendBrowserControl = reinterpret_cast<SendBrowserControlFn>(proc);
    return g_sendBrowserControl;
}

void SendBrowserControl(const char* command, const char* reason)
{
    auto sendControl = ResolveBrowserControl();
    if (!sendControl) return;

    char json[128] = {};
    sprintf_s(json, "{\"type\":\"control\",\"command\":\"%s\",\"reason\":\"%s\"}",
        command, reason);
    const int sent = sendControl(json);
    Log(std::string("browser control ") + command +
        " reason=" + reason + " sent=" + std::to_string(sent));
}

void HandleExternalStateChange(uint8_t oldState, uint8_t finalState)
{
    const bool applyingExternal =
        ExternalPlaybackStateSync::IsApplying();
    if (oldState == 1 && finalState == 3)
    {
        g_browserPauseReason = BrowserPauseReason::AutoBlock;
        SendBrowserControl("pause", "auto_block");
    }
    else if ((oldState == 0 || oldState == 5) && finalState == 1)
    {
        g_browserPauseReason = BrowserPauseReason::None;
        SendBrowserControl("resume", "start");
    }
    else if (oldState == 4 && finalState == 1 &&
        g_browserPauseReason == BrowserPauseReason::AutoBlock)
    {
        g_browserPauseReason = BrowserPauseReason::None;
        SendBrowserControl("resume", "auto_block");
    }
    else if (oldState == 1 && finalState == 2)
    {
        g_browserPauseReason = applyingExternal ?
            BrowserPauseReason::External :
            BrowserPauseReason::Manual;
        if (!applyingExternal)
        {
            SendBrowserControl("pause", "manual");
        }
    }
    else if (oldState == 2 && finalState == 1 &&
        (g_browserPauseReason == BrowserPauseReason::Manual ||
         g_browserPauseReason == BrowserPauseReason::External))
    {
        const BrowserPauseReason reason = g_browserPauseReason;
        g_browserPauseReason = BrowserPauseReason::None;
        if (!applyingExternal)
        {
            SendBrowserControl("resume",
                reason == BrowserPauseReason::External ?
                "external_sync" : "manual");
        }
    }
    else if (finalState == 0)
    {
        g_browserPauseReason = BrowserPauseReason::None;
    }
}

int64_t __fastcall DetourSetPlayState(void* runtime, uint8_t newState)
{
    RuntimeEntryTitleRefresh::SetRuntime(runtime);
    ExternalPlaybackStateSync::SetRuntime(runtime);
    const uint8_t oldState = ReadU8(
        runtime, GameLayout::MusicRuntime::kPlayState);
    const uint32_t trackId = ReadU32(
        runtime, GameLayout::MusicRuntime::kCurrentTrackId);
    void* currentRuntime = ReadPtr(
        runtime, GameLayout::MusicRuntime::kCurrentRuntime);
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const int64_t result = g_original(runtime, newState);
    const uint8_t finalState = ReadU8(
        runtime, GameLayout::MusicRuntime::kPlayState);
    DynamicTrackTitleSync::ApplyTitlePendingOnGameThread();

    if (oldState != finalState || oldState != newState)
    {
        std::ostringstream oss;
        oss << "music play state " << StateName(oldState) << "(" << int(oldState) << ")"
            << " -> " << StateName(finalState) << "(" << int(finalState) << ")"
            << " requested=" << StateName(newState) << "(" << int(newState) << ")"
            << " trackId=" << HookUtils::HexU64(trackId)
            << " external=" << (trackId == SpecialTrackIds::kCustomTrackId ? 1 : 0)
            << " currentRuntime=" << currentRuntime
            << " callerRva=" << HookUtils::HexU64(caller - base);
        Log(oss.str());
    }
    if (trackId == SpecialTrackIds::kCustomTrackId)
    {
        HandleExternalStateChange(oldState, finalState);
    }
    return result;
}
}

namespace PlayStateMonitor
{
bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    g_logger = const_cast<Logger*>(&logger);
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(gameModule, ".text", textStart, textSize))
    {
        Log("play state monitor skipped: .text not found");
        return false;
    }

    const uintptr_t target = PatternScan::Find(textStart, textSize, kSetPlayStatePattern);
    if (!target)
    {
        Log("play state monitor skipped: SetPlayState pattern not found");
        return false;
    }

    if (!InstallDetour(target, reinterpret_cast<void*>(&DetourSetPlayState),
        reinterpret_cast<void**>(&g_original)))
    {
        Log("play state monitor skipped: detour install failed");
        return false;
    }

    Log("play state monitor installed at rva=" +
        HookUtils::HexU64(target - reinterpret_cast<uintptr_t>(gameModule)));
    return true;
}
}
