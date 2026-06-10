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
HANDLE g_thread = nullptr;
HANDLE g_stopEvent = nullptr;
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
                if (WaitForStopSignal())
                {
                    break;
                }
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
                        track, update.titleText, update.artistText);
                }
            }
        }
        RuntimeEntryTitleRefresh::TryApplyPending();
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
}
