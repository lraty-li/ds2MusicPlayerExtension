#include "pch.h"

#include "RuntimeEntryTitleRefresh.h"

#include "GameLayout.h"
#include "SpecialTrackIds.h"

#include <atomic>

namespace
{
constexpr int32_t kMaxEntryCount = 1024;

using LocalizedTextToUiSharedStringFn = void* (__fastcall*)(void*, void**);
using UiSharedStringMoveAssignFn = void* (__fastcall*)(void**, void**);

HMODULE g_gameModule = nullptr;
std::atomic<uint32_t> g_generation{0};
std::atomic<uint32_t> g_appliedGeneration{0};
std::atomic<void*> g_track{nullptr};
std::atomic<void*> g_titleText{nullptr};
std::atomic<void*> g_artistText{nullptr};

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

        auto** runtimeSlot = ResolveGameRva<void**>(
            GameLayout::Rva::kMusicRuntimeGlobal);
        return runtimeSlot ? *runtimeSlot : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

void* FindCustomTrackEntry(void* runtime, void* preferredTrack)
{
    __try
    {
        if (!runtime)
        {
            return nullptr;
        }

        auto* runtimeBytes = static_cast<uint8_t*>(runtime);
        const int32_t count =
            *reinterpret_cast<int32_t*>(
                runtimeBytes + GameLayout::MusicRuntime::kEntryCount);
        auto* entries =
            *reinterpret_cast<uint8_t**>(
                runtimeBytes + GameLayout::MusicRuntime::kEntryData);
        if (!entries || count <= 0 || count > kMaxEntryCount)
        {
            return nullptr;
        }

        void* idMatchedEntry = nullptr;
        for (int32_t index = 0; index < count; ++index)
        {
            auto* entry = entries + static_cast<size_t>(index) *
                GameLayout::MusicEntry::kStride;
            auto* track =
                *reinterpret_cast<uint8_t**>(
                    entry + GameLayout::MusicEntry::kTrack);
            if (!track)
            {
                continue;
            }
            if (preferredTrack && track == preferredTrack)
            {
                return entry;
            }

            const uint32_t trackId =
                *reinterpret_cast<uint32_t*>(track + GameLayout::Track::kId);
            if (trackId == SpecialTrackIds::kCustomTrackId)
            {
                idMatchedEntry = entry;
            }
        }
        return idMatchedEntry;
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
                GameLayout::Rva::kLocalizedTextToUiSharedString);
        auto moveAssign =
            ResolveGameRva<UiSharedStringMoveAssignFn>(
                GameLayout::Rva::kUiSharedStringMoveAssign);

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
void Configure(HMODULE gameModule)
{
    g_gameModule = gameModule;
}

void Reset()
{
    g_generation.store(0);
    g_appliedGeneration.store(0);
    g_track.store(nullptr);
    g_titleText.store(nullptr);
    g_artistText.store(nullptr);
}

void Request(void* track, void* titleText, void* artistText)
{
    if (track)
    {
        g_track.store(track);
    }
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
        return;
    }

    void* entry = FindCustomTrackEntry(runtime, g_track.load());
    if (!entry)
    {
        return;
    }

    void* titleText = g_titleText.load();
    void* artistText = g_artistText.load();
    const bool titleOk = !titleText ||
        ReplaceEntryString(entry, GameLayout::MusicEntry::kTitle, titleText);
    const bool artistOk = !artistText ||
        ReplaceEntryString(entry, GameLayout::MusicEntry::kArtist, artistText);
    if (!titleOk || !artistOk)
    {
        return;
    }

    g_appliedGeneration.store(generation);
}
}
