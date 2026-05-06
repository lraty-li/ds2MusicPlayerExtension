#include "pch.h"

#include "DynamicTrackTitleSync.h"

#include "SpecialTrackHelpers.h"

#include <atomic>
#include <cstring>
#include <string>

namespace
{
constexpr DWORD kUpdateIntervalMs = 1000;
constexpr size_t kTitleBytes = 1024;
constexpr uint32_t kTrackTitleOffset = 0x38;
constexpr uint32_t kAlbumArtistOffset = 0x30;
constexpr uint32_t kAlbumTelopArtistOffset = 0x40;

using ReadMetadataFn = int(__cdecl*)(char*, unsigned int, char*, unsigned int);

std::atomic<void*> g_track{nullptr};
std::atomic<void*> g_album{nullptr};
std::atomic<bool> g_running{false};
Logger* g_logger = nullptr;
ReadMetadataFn g_readMetadata = nullptr;
void* g_titleObject = nullptr;
void* g_artistObject = nullptr;
void* g_telopArtistObject = nullptr;
char* g_titleBuffer = nullptr;
char* g_artistBuffer = nullptr;

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

bool PrepareMutableText(void* owner, uint32_t offset, void*& textObject, char*& buffer)
{
    __try
    {
        if (!buffer)
        {
            buffer = static_cast<char*>(SpecialTrackHelpers::HeapAllocZero(kTitleBytes));
        }
        if (!buffer)
        {
            return false;
        }

        textObject = *reinterpret_cast<void**>(static_cast<uint8_t*>(owner) + offset);
        if (!textObject)
        {
            return false;
        }

        auto* textBytes = static_cast<uint8_t*>(textObject);
        char* oldText = *reinterpret_cast<char**>(textBytes + 0x20);
        if (oldText && oldText[0])
        {
            strcpy_s(buffer, kTitleBytes, oldText);
        }
        *reinterpret_cast<char**>(textBytes + 0x20) = buffer;
        const size_t length = strlen(buffer);
        *reinterpret_cast<uint16_t*>(textBytes + 0x28) = static_cast<uint16_t>(length);
        *reinterpret_cast<int16_t*>(textBytes + 0x2A) = 0;
        if (oldText && oldText != buffer)
        {
            HeapFree(GetProcessHeap(), 0, oldText);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool UpdateMutableText(void* textObject, char* buffer, const char* value)
{
    __try
    {
        if (!textObject || !buffer)
        {
            return false;
        }

        strcpy_s(buffer, kTitleBytes, value ? value : "");
        const size_t length = strlen(buffer);
        auto* textBytes = static_cast<uint8_t*>(textObject);
        *reinterpret_cast<uint16_t*>(textBytes + 0x28) = static_cast<uint16_t>(length);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

DWORD WINAPI SyncThread(LPVOID)
{
    std::string lastTitle;
    std::string lastArtist;
    while (g_running.load())
    {
        void* track = g_track.load();
        void* album = g_album.load();
        auto readMetadata = ResolveReadMetadata();
        if (track && readMetadata)
        {
            if (!g_titleObject &&
                !PrepareMutableText(track, kTrackTitleOffset, g_titleObject, g_titleBuffer))
            {
                Sleep(kUpdateIntervalMs);
                continue;
            }
            if (album && !g_artistObject)
            {
                PrepareMutableText(album, kAlbumArtistOffset, g_artistObject, g_artistBuffer);
                PrepareMutableText(album, kAlbumTelopArtistOffset, g_telopArtistObject,
                    g_artistBuffer);
            }

            char title[kTitleBytes] = {};
            char artist[kTitleBytes] = {};
            if (readMetadata(title, static_cast<unsigned int>(sizeof(title)),
                artist, static_cast<unsigned int>(sizeof(artist))) && title[0])
            {
                if (strcmp(title, lastTitle.c_str()) != 0 &&
                    UpdateMutableText(g_titleObject, g_titleBuffer, title))
                {
                    lastTitle = title;
                    Log(std::string("dynamic title sync set title=\"") + title + "\"");
                }
                if (strcmp(artist, lastArtist.c_str()) != 0 &&
                    UpdateMutableText(g_artistObject, g_artistBuffer, artist))
                {
                    UpdateMutableText(g_telopArtistObject, g_artistBuffer, artist);
                    lastArtist = artist;
                    Log(std::string("dynamic title sync set artist=\"") + artist + "\"");
                }
            }
        }
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
    g_titleObject = nullptr;
    g_artistObject = nullptr;
    g_telopArtistObject = nullptr;
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
