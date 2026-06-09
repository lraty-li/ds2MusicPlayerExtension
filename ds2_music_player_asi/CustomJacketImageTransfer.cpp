#include "pch.h"

#include "CustomJacketImageTransfer.h"

#include "CustomJacketInternal.h"
#include "HookUtils.h"

#include <atomic>
#include <mutex>
#include <sstream>
#include <vector>

namespace
{
using ReadInfoFn = int(__cdecl*)(unsigned int*, unsigned int*, char*, unsigned int);
using ReadBytesFn = int(__cdecl*)(unsigned int, void*, unsigned int,
    unsigned int*, char*, unsigned int);
using ReadStatusFn = int(__cdecl*)(unsigned int*, char*, unsigned int,
    unsigned int*, char*, unsigned int, char*, unsigned int,
    char*, unsigned int, char*, unsigned int);

constexpr DWORD kPollMs = 1000;
constexpr unsigned int kTargetWidth = 640;
constexpr unsigned int kTargetHeight = 640;
constexpr unsigned int kMaxJacketBytes = 2 * 1024 * 1024;

std::atomic<bool> g_started{false};
Logger* g_logger = nullptr;
ReadInfoFn g_readInfo = nullptr;
ReadBytesFn g_readBytes = nullptr;
ReadStatusFn g_readStatus = nullptr;
std::mutex g_pendingMutex;
std::vector<uint8_t> g_pendingImage;
std::string g_pendingMime;
unsigned int g_pendingVersion = 0;
uint64_t g_uploadedResource = 0;
unsigned int g_uploadedVersion = 0;
unsigned int g_failedVersion = 0;

void Log(const std::string& text)
{
    if (g_logger) g_logger->Log(text);
}

bool ResolveExports()
{
    HMODULE module = GetModuleHandleW(L"ds2_dll_music_resource.dll");
    if (!module) return false;
    g_readInfo = reinterpret_cast<ReadInfoFn>(
        GetProcAddress(module, "DS2AudioStreamReadJacketInfo"));
    g_readBytes = reinterpret_cast<ReadBytesFn>(
        GetProcAddress(module, "DS2AudioStreamReadJacketBytes"));
    g_readStatus = reinterpret_cast<ReadStatusFn>(
        GetProcAddress(module, "DS2AudioStreamReadJacketStatus"));
    return g_readInfo && g_readBytes;
}

std::string HexPreview(const std::vector<uint8_t>& data)
{
    std::ostringstream oss;
    const size_t count = data.size() < 8 ? data.size() : 8;
    for (size_t i = 0; i < count; ++i)
    {
        if (i) oss << ' ';
        oss << HookUtils::HexU64(data[i]);
    }
    return oss.str();
}

void StorePendingImage(const std::vector<uint8_t>& buffer, unsigned int version,
    const char* mime)
{
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    g_pendingImage = buffer;
    g_pendingMime = mime ? mime : "";
    g_pendingVersion = version;
}

bool DecodeAndUploadToActiveResource(const std::vector<uint8_t>& buffer,
    unsigned int version, unsigned int written, const char* mime, const char* reason)
{
    if (buffer.empty() || written != buffer.size() || written > kMaxJacketBytes) return false;
    const uint64_t resource = CustomJacketInternal::GetActiveCustomJacketD3D12Resource();
    if (!resource)
    {
        return false;
    }
    if (resource == g_uploadedResource && version == g_uploadedVersion)
    {
        return true;
    }
    if (version == g_failedVersion)
    {
        return false;
    }

    std::vector<uint8_t> rgba;
    unsigned int sourceW = 0;
    unsigned int sourceH = 0;
    unsigned int drawW = 0;
    unsigned int drawH = 0;
    const bool decoded = CustomJacketInternal::TryDecodeCustomJacketImageToRgba(
        buffer.data(), written, kTargetWidth, kTargetHeight, rgba,
        sourceW, sourceH, drawW, drawH, *g_logger);
    const bool ok = decoded && CustomJacketInternal::TryUploadCustomJacketD3D12Pixels(
        resource, rgba.data(), kTargetWidth, kTargetHeight, *g_logger);
    std::ostringstream oss;
    oss << "jacket image upload " << (ok ? "ok" : "failed")
        << " resource=" << HookUtils::HexU64(resource)
        << " encodedBytes=" << written
        << " target=" << kTargetWidth << "x" << kTargetHeight
        << " source=" << sourceW << "x" << sourceH
        << " draw=" << drawW << "x" << drawH
        << " mime=\"" << (mime ? mime : "") << "\""
        << " reason=" << (reason ? reason : "");
    Log(oss.str());
    if (ok)
    {
        g_uploadedResource = resource;
        g_uploadedVersion = version;
    }
    else
    {
        g_failedVersion = version;
    }
    return ok;
}

void TryUploadImageJacket(const std::vector<uint8_t>& buffer,
    unsigned int version, unsigned int written, const char* mime)
{
    if (written == 0 || written > kMaxJacketBytes)
    {
        return;
    }
    StorePendingImage(buffer, version, mime);
    if (!DecodeAndUploadToActiveResource(buffer, version, written, mime, "new"))
    {
        Log("jacket image upload deferred: active resource missing or decode failed");
    }
}

void TryUploadPendingImage()
{
    std::vector<uint8_t> buffer;
    std::string mime;
    unsigned int version = 0;
    {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        if (g_pendingImage.empty()) return;
        buffer = g_pendingImage;
        mime = g_pendingMime;
        version = g_pendingVersion;
    }
    DecodeAndUploadToActiveResource(buffer, version,
        static_cast<unsigned int>(buffer.size()), mime.c_str(), "pending");
}

DWORD WINAPI ProbeThread(LPVOID)
{
    unsigned int lastVersion = 0;
    unsigned int lastStatusVersion = 0;
    while (true)
    {
        if ((!g_readInfo || !g_readBytes) && !ResolveExports())
        {
            Sleep(kPollMs);
            continue;
        }

        unsigned int version = 0;
        unsigned int bytes = 0;
        char mime[96] = {};
        if (g_readInfo(&version, &bytes, mime, sizeof(mime)) && version != lastVersion)
        {
            std::vector<uint8_t> buffer(bytes <= kMaxJacketBytes ? bytes : 0);
            unsigned int written = 0;
            const int readVersion = buffer.empty() ? 0 :
                g_readBytes(0, buffer.data(),
                    static_cast<unsigned int>(buffer.size()),
                    &written, mime, sizeof(mime));
            std::ostringstream oss;
            oss << "jacket transfer version=" << version
                << " readVersion=" << readVersion
                << " bytes=" << bytes
                << " written=" << written
                << " mime=\"" << mime << "\""
                << " head=" << HexPreview(buffer);
            Log(oss.str());
            if (readVersion > 0)
            {
                TryUploadImageJacket(buffer, version, written, mime);
            }
            lastVersion = version;
        }
        TryUploadPendingImage();
        if (g_readStatus)
        {
            unsigned int statusVersion = 0;
            unsigned int statusBytes = 0;
            char stage[96] = {};
            char statusMime[96] = {};
            char error[288] = {};
            char source[384] = {};
            char jacketSource[80] = {};
            if (g_readStatus(&statusVersion, stage, sizeof(stage),
                &statusBytes, statusMime, sizeof(statusMime),
                error, sizeof(error), source, sizeof(source),
                jacketSource, sizeof(jacketSource)) &&
                statusVersion != lastStatusVersion)
            {
                std::ostringstream oss;
                oss << "jacket status version=" << statusVersion
                    << " stage=\"" << stage << "\""
                    << " bytes=" << statusBytes
                    << " mime=\"" << statusMime << "\""
                    << " sourceKind=\"" << jacketSource << "\""
                    << " source=\"" << source << "\""
                    << " error=\"" << error << "\"";
                Log(oss.str());
                lastStatusVersion = statusVersion;
            }
        }
        Sleep(kPollMs);
    }
}
}

namespace CustomJacketImageTransfer
{
void Start(const Logger& logger)
{
    if (g_started.exchange(true)) return;
    g_logger = const_cast<Logger*>(&logger);
    HANDLE thread = CreateThread(nullptr, 0, ProbeThread, nullptr, 0, nullptr);
    if (thread)
    {
        CloseHandle(thread);
        Log("jacket image transfer started");
    }
}
}
