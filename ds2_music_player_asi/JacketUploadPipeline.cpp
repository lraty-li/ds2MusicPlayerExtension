#include "pch.h"

#include "JacketUploadPipeline.h"

#include "CustomJacketImageLayout.h"
#include "CustomJacketInternal.h"
#include "HookUtils.h"

#include <sstream>

namespace
{
uint64_t g_uploadedResource = 0;
unsigned int g_uploadedVersion = 0;
unsigned int g_failedVersion = 0;
} // namespace

namespace JacketUploadPipeline
{
bool DecodeAndUpload(const std::vector<uint8_t>& buffer, unsigned int version,
    unsigned int written, const char* mime, const char* reason, const Logger& logger)
{
    if (buffer.empty() || written != buffer.size() || written > kMaxJacketBytes)
    {
        return false;
    }

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
    constexpr uint32_t targetW = CustomJacketImageLayout::kDefaultTargetWidth;
    constexpr uint32_t targetH = CustomJacketImageLayout::kDefaultTargetHeight;
    const bool decoded = CustomJacketInternal::TryDecodeCustomJacketImageToRgba(
        buffer.data(), written, targetW, targetH, rgba,
        sourceW, sourceH, drawW, drawH, logger);
    const bool ok = decoded && CustomJacketInternal::TryUploadCustomJacketD3D12Pixels(
        resource, rgba.data(), targetW, targetH, logger);

    std::ostringstream oss;
    oss << "jacket image upload " << (ok ? "ok" : "failed")
        << " resource=" << HookUtils::HexU64(resource)
        << " encodedBytes=" << written
        << " target=" << targetW << "x" << targetH
        << " source=" << sourceW << "x" << sourceH
        << " draw=" << drawW << "x" << drawH
        << " mime=\"" << (mime ? mime : "") << "\""
        << " reason=" << (reason ? reason : "");
    logger.Log(oss.str());

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
} // namespace JacketUploadPipeline
