#include "pch.h"

#include "DynamicTrackTitleSync.h"

#include "GameThreadDispatcher.h"
#include "LocalizedTrackText.h"
#include "RuntimeEntryTitleRefresh.h"

#include <atomic>
#include <mutex>
#include <string>

namespace
{
constexpr DWORD kUpdateIntervalMs = 1000;
constexpr size_t kMetadataBytes = 1024;

using ReadMetadataFn = int(__cdecl*)(char*, unsigned int, char*, unsigned int);

std::atomic<void*> g_track{nullptr};
std::atomic<void*> g_album{nullptr};
std::atomic<bool> g_running{false};
HANDLE g_thread = nullptr;
HANDLE g_stopEvent = nullptr;
Logger* g_logger = nullptr;
ReadMetadataFn g_readMetadata = nullptr;
LocalizedTrackText::State g_textState;
std::mutex g_pendingMutex;
std::string g_pendingTitle;
std::string g_pendingArtist;
uint32_t g_pendingGeneration = 0;
uint32_t g_appliedGeneration = 0;

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

bool WaitForStopSignal()
{
    HANDLE stopEvent = g_stopEvent;
    if (!stopEvent)
    {
        Sleep(kUpdateIntervalMs);
        return false;
    }

    const DWORD result = WaitForSingleObject(stopEvent, kUpdateIntervalMs);
    return result != WAIT_TIMEOUT;
}

bool StorePendingMetadata(const char* title, const char* artist)
{
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    const std::string nextTitle = title ? title : "";
    const std::string nextArtist = artist ? artist : "";
    if (nextTitle != g_pendingTitle || nextArtist != g_pendingArtist)
    {
        g_pendingTitle = nextTitle;
        g_pendingArtist = nextArtist;
        ++g_pendingGeneration;
    }
    return g_pendingGeneration != g_appliedGeneration;
}

void RequestApply()
{
    if (g_logger &&
        GameThreadDispatcher::EnsureInstalled(*g_logger) &&
        GameThreadDispatcher::Post(&DynamicTrackTitleSync::ApplyPendingOnGameThread))
    {
        return;
    }
}

DWORD WINAPI SyncThread(LPVOID)
{
    while (g_running.load())
    {
        void* track = g_track.load();
        auto readMetadata = ResolveReadMetadata();
        if (track && readMetadata)
        {
            char title[kMetadataBytes] = {};
            char artist[kMetadataBytes] = {};
            if (readMetadata(title, static_cast<unsigned int>(sizeof(title)),
                artist, static_cast<unsigned int>(sizeof(artist))) && title[0])
            {
                if (StorePendingMetadata(title, artist))
                {
                    RequestApply();
                }
            }
        }
        if (WaitForStopSignal())
        {
            break;
        }
    }

    return 0;
}
}

namespace DynamicTrackTitleSync
{
void Reset()
{
    const bool wasRunning = g_running.exchange(false);
    if (wasRunning && g_stopEvent)
    {
        SetEvent(g_stopEvent);
    }
    if (g_thread)
    {
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }

    g_track.store(nullptr);
    g_album.store(nullptr);
    g_readMetadata = nullptr;
    RuntimeEntryTitleRefresh::Reset();
    {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        g_pendingTitle.clear();
        g_pendingArtist.clear();
        g_pendingGeneration = 0;
        g_appliedGeneration = 0;
        LocalizedTrackText::Reset(g_textState);
    }
}

void Start(void* track, void* album, const Logger& logger)
{
    if (!track || g_running.exchange(true))
    {
        return;
    }

    g_logger = const_cast<Logger*>(&logger);
    if (!g_stopEvent)
    {
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    if (!g_stopEvent)
    {
        g_running.store(false);
        Log("dynamic title sync failed to create stop event");
        return;
    }

    ResetEvent(g_stopEvent);
    g_track.store(track);
    g_album.store(album);
    HANDLE thread = CreateThread(nullptr, 0, SyncThread, nullptr, 0, nullptr);
    if (thread)
    {
        g_thread = thread;
        Log("dynamic title sync started");
    }
    else
    {
        g_running.store(false);
        g_track.store(nullptr);
        g_album.store(nullptr);
        Log("dynamic title sync failed to start thread");
    }
}

void ApplyPendingOnGameThread()
{
    RuntimeEntryTitleRefresh::TryApplyPending();

    void* track = g_track.load();
    if (!track)
    {
        return;
    }

    std::string title;
    std::string artist;
    uint32_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        generation = g_pendingGeneration;
        if (!generation || g_appliedGeneration == generation)
        {
            return;
        }
        title = g_pendingTitle;
        artist = g_pendingArtist;
    }

    void* album = g_album.load();
    if (!LocalizedTrackText::Resolve(g_textState, track, album))
    {
        return;
    }

    auto update = LocalizedTrackText::Apply(g_textState,
        title.c_str(), artist.c_str());
    if (update.changed)
    {
        RuntimeEntryTitleRefresh::Request(
            track, update.titleText, update.artistText);
    }

    {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        if (g_pendingGeneration == generation)
        {
            g_appliedGeneration = generation;
        }
    }
    RuntimeEntryTitleRefresh::TryApplyPending();
}
}
