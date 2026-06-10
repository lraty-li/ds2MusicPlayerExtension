#include "pch.h"

#include "DynamicTrackTitleSync.h"

#include "LocalizedTrackText.h"
#include "RuntimeEntryTitleRefresh.h"

#include <atomic>
#include <string>

namespace
{
constexpr DWORD kUpdateIntervalMs = 1000;
constexpr size_t kMetadataBytes = 1024;

using ReadMetadataFn = int(__cdecl*)(char*, unsigned int, char*, unsigned int);

std::atomic<void*> g_track{nullptr};
std::atomic<void*> g_album{nullptr};
std::atomic<bool> g_running{false};
Logger* g_logger = nullptr;
ReadMetadataFn g_readMetadata = nullptr;
LocalizedTrackText::State g_textState;

void Log(const std::string& text)
{
    if (g_logger)
    {
        g_logger->Log(text);
    }
}

ReadMetadataFn ResolveReadMetadata()
{
    if (g_readMetadata) return g_readMetadata;
    HMODULE module = GetModuleHandleW(L"ds2_dll_music_resource.dll");
    if (!module) return nullptr;
    auto* proc = GetProcAddress(module, "DS2AudioStreamReadMetadata");
    g_readMetadata = reinterpret_cast<ReadMetadataFn>(proc);
    return g_readMetadata;
}

DWORD WINAPI SyncThread(LPVOID)
{
    while (g_running.load())
    {
        void* track = g_track.load();
        void* album = g_album.load();
        auto readMetadata = ResolveReadMetadata();
        if (track && readMetadata)
        {
            if (!LocalizedTrackText::Resolve(g_textState, track, album))
            {
                Sleep(kUpdateIntervalMs);
                continue;
            }

            char title[kMetadataBytes] = {};
            char artist[kMetadataBytes] = {};
            if (readMetadata(title, static_cast<unsigned int>(sizeof(title)),
                artist, static_cast<unsigned int>(sizeof(artist))) && title[0])
            {
                auto update = LocalizedTrackText::Apply(
                    g_textState, title, artist);
                if (update.changed)
                {
                    RuntimeEntryTitleRefresh::Request(
                        update.titleText, update.artistText);
                }
            }
        }
        RuntimeEntryTitleRefresh::TryApplyPending();
        Sleep(kUpdateIntervalMs);
    }

    return 0;
}
}

namespace DynamicTrackTitleSync
{
void Reset()
{
    g_running.store(false);
    g_track.store(nullptr);
    g_album.store(nullptr);
    g_readMetadata = nullptr;
    LocalizedTrackText::Reset(g_textState);
    RuntimeEntryTitleRefresh::Reset();
}

void Start(void* track, void* album, const Logger& logger)
{
    if (!track || g_running.exchange(true))
    {
        return;
    }

    g_logger = const_cast<Logger*>(&logger);
    g_track.store(track);
    g_album.store(album);
    HANDLE thread = CreateThread(nullptr, 0, SyncThread, nullptr, 0, nullptr);
    if (thread)
    {
        CloseHandle(thread);
        Log("dynamic title sync started");
    }
    else
    {
        g_running.store(false);
        Log("dynamic title sync failed to start thread");
    }
}
}
