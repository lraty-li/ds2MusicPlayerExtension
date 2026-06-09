#include "pch.h"

#include "BrowserJacket.h"

#include "PluginLog.h"

#include <cstring>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>
#include <wincrypt.h>

namespace
{
constexpr uint32_t kMaxJacketBytes = 2 * 1024 * 1024;
std::mutex g_mutex;
std::vector<uint8_t> g_bytes;
std::string g_mime;
std::string g_source;
uint32_t g_version = 0;
std::string g_statusStage;
std::string g_statusMime;
std::string g_statusError;
std::string g_statusSource;
std::string g_statusJacketSource;
uint32_t g_statusVersion = 0;
uint32_t g_statusBytes = 0;

bool Contains(const char* text, const char* needle)
{
    return text && needle && strstr(text, needle) != nullptr;
}

const char* FindJsonStringValue(const char* json, const char* key)
{
    const char* keyAt = strstr(json, key);
    if (!keyAt) return nullptr;
    const char* colon = strchr(keyAt + strlen(key), ':');
    if (!colon) return nullptr;
    const char* quote = strchr(colon, '"');
    return quote ? quote + 1 : nullptr;
}

void DecodeJsonString(const char* input, std::string& output)
{
    output.clear();
    if (!input) return;
    for (const char* at = input; *at && *at != '"'; ++at)
    {
        if (*at == '\\' && at[1])
        {
            ++at;
            output.push_back(*at == 'n' || *at == 'r' || *at == 't' ? ' ' : *at);
        }
        else
        {
            output.push_back(*at);
        }
    }
}

bool DecodeBase64(const std::string& text, std::vector<uint8_t>& out)
{
    DWORD needed = 0;
    if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
        CRYPT_STRING_BASE64, nullptr, &needed, nullptr, nullptr) || needed > kMaxJacketBytes)
    {
        return false;
    }
    out.assign(needed, 0);
    return CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
        CRYPT_STRING_BASE64, out.data(), &needed, nullptr, nullptr) != FALSE;
}

void CopyString(char* output, uint32_t outputBytes, const std::string& value)
{
    if (!output || outputBytes == 0) return;
    strcpy_s(output, outputBytes, value.c_str());
}

void SetStatus(const char* stage, uint32_t bytes,
    const std::string& mime, const std::string& error,
    const std::string& source = "", const std::string& jacketSource = "")
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_statusStage = stage ? stage : "";
    g_statusBytes = bytes;
    g_statusMime = mime;
    g_statusError = error;
    g_statusSource = source;
    g_statusJacketSource = jacketSource;
    ++g_statusVersion;
}

void LogUpdate(uint32_t bytes, const std::string& mime, const std::string& source)
{
    char line[256] = {};
    sprintf_s(line, "browser jacket image bytes=%u mime=\"%s\" source=\"%s\"",
        bytes, mime.c_str(), source.c_str());
    PluginLog::Write(line);
}
}

namespace BrowserJacket
{
void UpdateStatusFromJson(const char* json)
{
    if (!Contains(json, "\"type\":\"jacket_status\"") &&
        !Contains(json, "\"type\": \"jacket_status\""))
    {
        return;
    }

    std::string stage;
    std::string mime;
    std::string error;
    std::string source;
    std::string jacketSource;
    DecodeJsonString(FindJsonStringValue(json, "\"stage\""), stage);
    DecodeJsonString(FindJsonStringValue(json, "\"mime\""), mime);
    DecodeJsonString(FindJsonStringValue(json, "\"error\""), error);
    DecodeJsonString(FindJsonStringValue(json, "\"source\""), source);
    DecodeJsonString(FindJsonStringValue(json, "\"jacketSource\""), jacketSource);
    const char* bytesAt = strstr(json, "\"bytes\"");
    uint32_t bytes = 0;
    if (bytesAt)
    {
        const char* colon = strchr(bytesAt, ':');
        bytes = colon ? static_cast<uint32_t>(strtoul(colon + 1, nullptr, 10)) : 0;
    }
    SetStatus(stage.c_str(), bytes, mime, error, source, jacketSource);
}

void UpdateFromJson(const char* json)
{
    if (!Contains(json, "\"type\":\"jacket\"") &&
        !Contains(json, "\"type\": \"jacket\""))
    {
        return;
    }

    std::string data;
    std::string mime;
    std::string source;
    std::string jacketSource;
    const char* dataValue = FindJsonStringValue(json, "\"data\"");
    if (!dataValue)
    {
        SetStatus("runtime_missing_data", 0, "", "missing data field");
        return;
    }
    DecodeJsonString(dataValue, data);
    DecodeJsonString(FindJsonStringValue(json, "\"mime\""), mime);
    DecodeJsonString(FindJsonStringValue(json, "\"source\""), source);
    DecodeJsonString(FindJsonStringValue(json, "\"jacketSource\""), jacketSource);
    if (mime.empty()) mime = "application/octet-stream";

    std::vector<uint8_t> decoded;
    if (!DecodeBase64(data, decoded) || decoded.empty())
    {
        SetStatus("runtime_decode_failed", 0, mime, "base64 decode failed",
            source, jacketSource);
        return;
    }
    const uint32_t decodedBytes = static_cast<uint32_t>(decoded.size());
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_bytes.swap(decoded);
        g_mime = mime;
        g_source = source;
        ++g_version;
    }
    SetStatus("runtime_cached", decodedBytes, mime, "", source, jacketSource);
    LogUpdate(decodedBytes, mime, source);
}

int ReadInfo(uint32_t* version, uint32_t* bytes, char* mime, uint32_t mimeBytes)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (version) *version = g_version;
    if (bytes) *bytes = static_cast<uint32_t>(g_bytes.size());
    CopyString(mime, mimeBytes, g_mime);
    return !g_bytes.empty() ? 1 : 0;
}

int ReadBytes(uint32_t knownVersion, void* output, uint32_t outputBytes,
    uint32_t* bytesWritten, char* mime, uint32_t mimeBytes)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (bytesWritten) *bytesWritten = 0;
    CopyString(mime, mimeBytes, g_mime);
    if (knownVersion == g_version || g_bytes.empty() || !output) return 0;
    if (outputBytes < g_bytes.size()) return -1;
    memcpy(output, g_bytes.data(), g_bytes.size());
    if (bytesWritten) *bytesWritten = static_cast<uint32_t>(g_bytes.size());
    return static_cast<int>(g_version);
}

int ReadStatus(uint32_t* version, char* stage, uint32_t stageBytes,
    uint32_t* bytes, char* mime, uint32_t mimeBytes,
    char* error, uint32_t errorBytes, char* source, uint32_t sourceBytes,
    char* jacketSource, uint32_t jacketSourceBytes)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (version) *version = g_statusVersion;
    if (bytes) *bytes = g_statusBytes;
    CopyString(stage, stageBytes, g_statusStage);
    CopyString(mime, mimeBytes, g_statusMime);
    CopyString(error, errorBytes, g_statusError);
    CopyString(source, sourceBytes, g_statusSource);
    CopyString(jacketSource, jacketSourceBytes, g_statusJacketSource);
    return g_statusVersion ? 1 : 0;
}
}
