#include "pch.h"

#include "BrowserMetadata.h"

#include "PluginLog.h"

#include <cstring>
#include <mutex>

namespace
{
constexpr size_t kTitleBytes = 1024;
constexpr size_t kTrackKeyBytes = 1024;
constexpr char kPausedPrefix[] = "[PAUSED] ";

std::mutex g_mutex;
char g_title[kTitleBytes] = {};
char g_artist[kTitleBytes] = {};
char g_trackKey[kTrackKeyBytes] = {};
bool g_hasPausedState = false;
bool g_paused = false;
uint32_t g_playbackStateVersion = 0;

const char* FindJsonStringValue(const char* json, const char* key)
{
    const char* keyAt = strstr(json, key);
    if (!keyAt) return nullptr;
    const char* colon = strchr(keyAt + strlen(key), ':');
    if (!colon) return nullptr;
    const char* quote = strchr(colon, '"');
    return quote ? quote + 1 : nullptr;
}

void DecodeJsonString(const char* input, char* output, size_t outputBytes)
{
    size_t used = 0;
    for (const char* at = input; *at && *at != '"' && used + 1 < outputBytes; ++at)
    {
        if (*at == '\\' && at[1])
        {
            ++at;
            if (*at == 'n' || *at == 'r' || *at == 't')
            {
                output[used++] = ' ';
            }
            else
            {
                output[used++] = *at;
            }
        }
        else
        {
            output[used++] = *at;
        }
    }
    output[used] = 0;
}

bool IsMetadataMessage(const char* json)
{
    return strstr(json, "\"type\":\"metadata\"") ||
        strstr(json, "\"type\": \"metadata\"");
}

void CopyJsonValue(const char* json, const char* key, char* output, size_t outputBytes)
{
    output[0] = 0;
    const char* value = FindJsonStringValue(json, key);
    if (value)
    {
        DecodeJsonString(value, output, outputBytes);
    }
}

bool ReadJsonBool(const char* json, const char* key, bool& value)
{
    const char* keyAt = strstr(json, key);
    if (!keyAt) return false;
    const char* at = strchr(keyAt + strlen(key), ':');
    if (!at) return false;
    do
    {
        ++at;
    } while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n');
    if (strncmp(at, "true", 4) == 0)
    {
        value = true;
        return true;
    }
    if (strncmp(at, "false", 5) == 0)
    {
        value = false;
        return true;
    }
    return false;
}

void CopyDisplayTitle(char* output, uint32_t outputBytes)
{
    if (g_hasPausedState && g_paused)
    {
        strncpy_s(output, outputBytes, kPausedPrefix, _TRUNCATE);
        strncat_s(output, outputBytes, g_title, _TRUNCATE);
        return;
    }
    strcpy_s(output, outputBytes, g_title);
}

}

namespace BrowserMetadata
{
void UpdateFromJson(const char* json)
{
    if (!json || !IsMetadataMessage(json)) return;
    char title[kTitleBytes] = {};
    char artist[kTitleBytes] = {};
    char trackKey[kTrackKeyBytes] = {};
    bool paused = false;
    const bool hasPausedState = ReadJsonBool(json, "\"paused\"", paused);
    CopyJsonValue(json, "\"title\"", title, sizeof(title));
    if (!title[0]) return;
    CopyJsonValue(json, "\"artist\"", artist, sizeof(artist));
    CopyJsonValue(json, "\"trackKey\"", trackKey, sizeof(trackKey));
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (strcmp(g_title, title) == 0 && strcmp(g_artist, artist) == 0 &&
            strcmp(g_trackKey, trackKey) == 0 &&
            g_hasPausedState == hasPausedState &&
            (!hasPausedState || g_paused == paused))
        {
            return;
        }
        const bool playbackStateChanged =
            g_hasPausedState != hasPausedState ||
            (hasPausedState && g_paused != paused);
        strcpy_s(g_title, title);
        strcpy_s(g_artist, artist);
        strcpy_s(g_trackKey, trackKey);
        g_hasPausedState = hasPausedState;
        g_paused = paused;
        if (playbackStateChanged)
        {
            ++g_playbackStateVersion;
            if (!g_playbackStateVersion) ++g_playbackStateVersion;
        }
    }

    char line[1152] = {};
    sprintf_s(line,
        "browser metadata title=\"%s\" artist=\"%s\" paused=%s",
        title, artist,
        hasPausedState ? (paused ? "true" : "false") : "unknown");
    PluginLog::Write(line);
}

bool IsCurrentTrackKey(const char* trackKey)
{
    if (!trackKey || !trackKey[0])
    {
        return true;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_trackKey[0] && strcmp(g_trackKey, trackKey) == 0;
}

int ReadTitle(char* output, uint32_t outputBytes)
{
    if (!output || outputBytes == 0) return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    CopyDisplayTitle(output, outputBytes);
    return g_title[0] ? 1 : 0;
}

int Read(char* title, uint32_t titleBytes, char* artist, uint32_t artistBytes)
{
    if (!title || titleBytes == 0 || !artist || artistBytes == 0) return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    CopyDisplayTitle(title, titleBytes);
    strcpy_s(artist, artistBytes, g_artist);
    return g_title[0] ? 1 : 0;
}

int ReadPlaybackState(uint32_t* version, int* known, int* paused)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (version) *version = g_playbackStateVersion;
    if (known) *known = g_hasPausedState ? 1 : 0;
    if (paused) *paused = g_paused ? 1 : 0;
    return g_playbackStateVersion ? 1 : 0;
}
}
