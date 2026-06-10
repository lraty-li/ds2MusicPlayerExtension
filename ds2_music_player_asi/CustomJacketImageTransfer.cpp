#include "pch.h"

#include "CustomJacketImageTransfer.h"

#include "HookUtils.h"
#include "JacketPendingImage.h"
#include "JacketUploadPipeline.h"
#include "RuntimeJacketBridge.h"

#include <atomic>
#include <sstream>
#include <vector>

namespace
{
constexpr DWORD kPollMs = 200;

std::atomic<bool> g_started{false};
Logger* g_logger = nullptr;

void Log(const std::string& text)
{
    if (g_logger) g_logger->Log(text);
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

void TryUploadImageJacket(const std::vector<uint8_t>& buffer,
    unsigned int version, unsigned int written, const char* mime)
{
    if (!g_logger || written == 0 || written > JacketUploadPipeline::kMaxJacketBytes)
    {
        return;
    }
    JacketPendingImage::Store(buffer, version, mime);
    if (!JacketUploadPipeline::DecodeAndUpload(buffer, version, written,
        mime, "new", *g_logger))
    {
        Log("jacket image upload deferred: active resource missing or decode failed");
    }
}

void TryUploadPendingImage()
{
    if (!g_logger) return;
    JacketPendingImage::Image image;
    if (!JacketPendingImage::Snapshot(image)) return;
    JacketUploadPipeline::DecodeAndUpload(image.bytes, image.version,
        static_cast<unsigned int>(image.bytes.size()), image.mime.c_str(),
        "pending", *g_logger);
}

DWORD WINAPI TransferThread(LPVOID)
{
    unsigned int lastVersion = 0;
    unsigned int lastStatusVersion = 0;
    while (true)
    {
        if (!RuntimeJacketBridge::EnsureReady())
        {
            Sleep(kPollMs);
            continue;
        }

        RuntimeJacketBridge::JacketInfo info;
        if (RuntimeJacketBridge::ReadInfo(info) && info.version != lastVersion)
        {
            std::vector<uint8_t> buffer(
                info.bytes <= JacketUploadPipeline::kMaxJacketBytes ? info.bytes : 0);
            unsigned int written = 0;
            const int readVersion = buffer.empty() ? 0 :
                RuntimeJacketBridge::ReadBytes(0, buffer.data(),
                    static_cast<unsigned int>(buffer.size()), written,
                    info.mime, sizeof(info.mime));
            std::ostringstream oss;
            oss << "jacket transfer version=" << info.version
                << " readVersion=" << readVersion
                << " bytes=" << info.bytes
                << " written=" << written
                << " mime=\"" << info.mime << "\""
                << " head=" << HexPreview(buffer);
            Log(oss.str());
            if (readVersion > 0)
            {
                TryUploadImageJacket(buffer, info.version, written, info.mime);
            }
            lastVersion = info.version;
        }
        TryUploadPendingImage();

        RuntimeJacketBridge::JacketStatus status;
        if (RuntimeJacketBridge::ReadStatus(status) &&
            status.version != lastStatusVersion)
        {
            std::ostringstream oss;
            oss << "jacket status version=" << status.version
                << " stage=\"" << status.stage << "\""
                << " bytes=" << status.bytes
                << " mime=\"" << status.mime << "\""
                << " sourceKind=\"" << status.jacketSource << "\""
                << " source=\"" << status.source << "\""
                << " error=\"" << status.error << "\"";
            Log(oss.str());
            lastStatusVersion = status.version;
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
    HANDLE thread = CreateThread(nullptr, 0, TransferThread, nullptr, 0, nullptr);
    if (thread)
    {
        CloseHandle(thread);
        Log("jacket image transfer started");
    }
}
}
