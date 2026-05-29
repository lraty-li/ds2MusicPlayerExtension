#include "pch.h"

#include "PlayEntryMonitor.h"

#include "HookUtils.h"

#include <sstream>

namespace
{
constexpr uint32_t kPatchBytes = 13;
constexpr uintptr_t kPlayEntryRva = 0xC12580;
constexpr uint32_t kStateOffset = 0x1910;
constexpr uint32_t kCurrentRuntimeOffset = 0x1918;
constexpr uint32_t kCurrentTrackIdOffset = 0x1924;
constexpr uint32_t kEntryTrackOffset = 0x10;
constexpr uint32_t kTrackIdOffset = 0x20;
constexpr uint32_t kTrackTitleOffset = 0x38;
constexpr uint32_t kTrackSoundResourceOffset = 0x40;

using PlayEntryFn = int64_t(__fastcall*)(void*, void*);

Logger* g_logger = nullptr;
PlayEntryFn g_original = nullptr;

void Log(const std::string& text)
{
    if (g_logger) g_logger->Log(text);
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

const char* ReadTextValue(void* textObject)
{
    __try
    {
        if (!textObject) return "";
        const auto* textBytes = static_cast<uint8_t*>(textObject);
        const char* value = *reinterpret_cast<const char* const*>(textBytes + 0x20);
        return value ? value : "";
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return "";
    }
}

std::string RuntimeStateText(void* runtime)
{
    std::ostringstream oss;
    const uint8_t state = ReadU8(runtime, kStateOffset);
    oss << StateName(state) << "(" << int(state) << ")"
        << " currentRuntime=" << ReadPtr(runtime, kCurrentRuntimeOffset)
        << " currentTrackId=" << HookUtils::HexU64(ReadU32(runtime, kCurrentTrackIdOffset));
    return oss.str();
}

int64_t __fastcall DetourPlayEntry(void* runtime, void* entry)
{
    void* track = ReadPtr(entry, kEntryTrackOffset);
    void* title = ReadPtr(track, kTrackTitleOffset);
    void* sound = ReadPtr(track, kTrackSoundResourceOffset);
    const uint32_t trackId = ReadU32(track, kTrackIdOffset);

    std::ostringstream pre;
    pre << "play entry enter runtime=" << runtime
        << " entry=" << entry
        << " track=" << track
        << " trackId=" << HookUtils::HexU64(trackId)
        << " title=\"" << ReadTextValue(title) << "\""
        << " sound=" << sound
        << " state={" << RuntimeStateText(runtime) << "}";
    Log(pre.str());

    const int64_t result = g_original(runtime, entry);

    std::ostringstream post;
    post << "play entry leave result=" << HookUtils::HexU64(result)
        << " state={" << RuntimeStateText(runtime) << "}";
    Log(post.str());
    return result;
}
}

namespace PlayEntryMonitor
{
bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    g_logger = const_cast<Logger*>(&logger);
    const uintptr_t target = reinterpret_cast<uintptr_t>(gameModule) + kPlayEntryRva;
    if (!HookUtils::IsAddressRangeInModule(gameModule, target, kPatchBytes))
    {
        Log("play entry monitor skipped: address outside module");
        return false;
    }

    if (!InstallDetour(target, reinterpret_cast<void*>(&DetourPlayEntry),
        reinterpret_cast<void**>(&g_original)))
    {
        Log("play entry monitor skipped: detour install failed");
        return false;
    }

    Log("play entry monitor installed at rva=" + HookUtils::HexU64(kPlayEntryRva));
    return true;
}
}
