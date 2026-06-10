#include "pch.h"

#include "RuntimeEntryTitleRefresh.h"

#include "SpecialTrackIds.h"

#include <atomic>
#include <sstream>
#include <string>

namespace
{
constexpr uintptr_t kMusicRuntimeGlobalRva = 0x623E5F0;
constexpr uintptr_t kLocalizedTextToUiSharedStringRva = 0x27023A0;
constexpr uintptr_t kUiSharedStringMoveAssignRva = 0x0A3920;
constexpr uint32_t kEntryCountOffset = 0x1938;
constexpr uint32_t kEntryDataOffset = 0x1940;
constexpr uint32_t kEntryStride = 0x38;
constexpr uint32_t kEntryTitleOffset = 0x00;
constexpr uint32_t kEntryArtistOffset = 0x08;
constexpr uint32_t kEntryTrackOffset = 0x10;
constexpr uint32_t kTrackIdOffset = 0x20;
constexpr int32_t kMaxEntryCount = 1024;

using LocalizedTextToUiSharedStringFn = void* (__fastcall*)(void*, void**);
using UiSharedStringMoveAssignFn = void* (__fastcall*)(void**, void**);

HMODULE g_gameModule = nullptr;
Logger* g_logger = nullptr;
std::atomic<uint32_t> g_generation{0};
std::atomic<uint32_t> g_appliedGeneration{0};
std::atomic<void*> g_titleText{nullptr};
std::atomic<void*> g_artistText{nullptr};
bool g_loggedMissingRuntime = false;
bool g_loggedMissingEntry = false;

void Log(const std::string& text)
{
    if (g_logger)
    {
        g_logger->Log(text);
    }
}

uintptr_t GameBase()
{
    return reinterpret_cast<uintptr_t>(g_gameModule);
}

template <typename T>
T ResolveGameRva(uintptr_t rva)
{
    return reinterpret_cast<T>(GameBase() + rva);
}

void* ReadMusicRuntime()
{
    __try
    {
        if (!g_gameModule)
        {
            return nullptr;
        }

        auto** runtimeSlot = ResolveGameRva<void**>(kMusicRuntimeGlobalRva);
        return runtimeSlot ? *runtimeSlot : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

void* FindCustomTrackEntry(void* runtime)
{
    __try
    {
        if (!runtime)
        {
            return nullptr;
        }

        auto* runtimeBytes = static_cast<uint8_t*>(runtime);
        const int32_t count =
            *reinterpret_cast<int32_t*>(runtimeBytes + kEntryCountOffset);
        auto* entries =
            *reinterpret_cast<uint8_t**>(runtimeBytes + kEntryDataOffset);
        if (!entries || count <= 0 || count > kMaxEntryCount)
        {
            return nullptr;
        }

        for (int32_t index = 0; index < count; ++index)
        {
            auto* entry = entries + static_cast<size_t>(index) * kEntryStride;
            auto* track =
                *reinterpret_cast<uint8_t**>(entry + kEntryTrackOffset);
            if (!track)
            {
                continue;
            }

            const uint32_t trackId =
                *reinterpret_cast<uint32_t*>(track + kTrackIdOffset);
            if (trackId == SpecialTrackIds::kCustomTrackId)
            {
                return entry;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }

    return nullptr;
}

bool ReplaceEntryString(void* entry, uint32_t offset, void* localizedText)
{
    __try
    {
        if (!entry || !localizedText || !g_gameModule)
        {
            return false;
        }

        auto toUiSharedString =
            ResolveGameRva<LocalizedTextToUiSharedStringFn>(
                kLocalizedTextToUiSharedStringRva);
        auto moveAssign =
            ResolveGameRva<UiSharedStringMoveAssignFn>(
                kUiSharedStringMoveAssignRva);

        void* newSlot = nullptr;
        toUiSharedString(localizedText, &newSlot);
        if (!newSlot)
        {
            return false;
        }

        auto** target = reinterpret_cast<void**>(
            static_cast<uint8_t*>(entry) + offset);
        moveAssign(target, &newSlot);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
}

namespace RuntimeEntryTitleRefresh
{
void Configure(HMODULE gameModule, const Logger& logger)
{
    g_gameModule = gameModule;
    g_logger = const_cast<Logger*>(&logger);
    Log("runtime entry title refresh configured");
}

void Reset()
{
    g_generation.store(0);
    g_appliedGeneration.store(0);
    g_titleText.store(nullptr);
    g_artistText.store(nullptr);
    g_loggedMissingRuntime = false;
    g_loggedMissingEntry = false;
}

void Request(void* titleText, void* artistText)
{
    if (titleText)
    {
        g_titleText.store(titleText);
    }
    if (artistText)
    {
        g_artistText.store(artistText);
    }

    g_generation.fetch_add(1);
    TryApplyPending();
}

void TryApplyPending()
{
    const uint32_t generation = g_generation.load();
    if (!generation || g_appliedGeneration.load() == generation)
    {
        return;
    }

    void* runtime = ReadMusicRuntime();
    if (!runtime)
    {
        if (!g_loggedMissingRuntime)
        {
            Log("runtime entry title refresh waiting for music runtime");
            g_loggedMissingRuntime = true;
        }
        return;
    }
    g_loggedMissingRuntime = false;

    void* entry = FindCustomTrackEntry(runtime);
    if (!entry)
    {
        if (!g_loggedMissingEntry)
        {
            Log("runtime entry title refresh waiting for custom entry");
            g_loggedMissingEntry = true;
        }
        return;
    }
    g_loggedMissingEntry = false;

    void* titleText = g_titleText.load();
    void* artistText = g_artistText.load();
    const bool titleOk = !titleText ||
        ReplaceEntryString(entry, kEntryTitleOffset, titleText);
    const bool artistOk = !artistText ||
        ReplaceEntryString(entry, kEntryArtistOffset, artistText);
    if (!titleOk || !artistOk)
    {
        return;
    }

    g_appliedGeneration.store(generation);
    std::ostringstream oss;
    oss << "runtime entry title refresh applied generation=" << generation
        << " entry=" << entry;
    Log(oss.str());
}
}
