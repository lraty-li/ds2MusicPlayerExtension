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

void LogDiagnosticDecision(const char* decision, uint64_t resource,
    unsigned int version, const Logger& logger)
{
#if defined(DS2_DIAGNOSTIC)
    static std::string lastDecision;
    static uint64_t lastResource = 0;
    static unsigned int lastVersion = 0;
    static uint64_t lastUploadedResource = 0;
    static unsigned int lastUploadedVersion = 0;
    static unsigned int lastFailedVersion = 0;
    if (lastDecision == decision && lastResource == resource && lastVersion == version
        && lastUploadedResource == g_uploadedResource
        && lastUploadedVersion == g_uploadedVersion
        && lastFailedVersion == g_failedVersion)
    {
        return;
    }

    std::ostringstream oss;
    oss << "diag jacket upload " << decision
        << " resource=" << HookUtils::HexU64(resource)
        << " version=" << version
        << " uploadedResource=" << HookUtils::HexU64(g_uploadedResource)
        << " uploadedVersion=" << g_uploadedVersion
        << " failedVersion=" << g_failedVersion;
    logger.Log(oss.str());
    lastDecision = decision;
    lastResource = resource;
    lastVersion = version;
    lastUploadedResource = g_uploadedResource;
    lastUploadedVersion = g_uploadedVersion;
    lastFailedVersion = g_failedVersion;
#else
    (void)decision;
    (void)resource;
    (void)version;
    (void)logger;
#endif
}
} // namespace

namespace JacketUploadPipeline
{
bool DecodeAndUpload(const std::vector<uint8_t>& buffer, unsigned int version,
    unsigned int written, const char* mime, const char* reason, const Logger& logger)
{
    if (buffer.empty() || written != buffer.size() || written > kMaxJacketBytes)
    {
        LogDiagnosticDecision("rejected-input", 0, version, logger);
        return false;
    }

    const uint64_t resource = CustomJacketInternal::GetActiveCustomJacketD3D12Resource();
    if (!resource)
    {
        LogDiagnosticDecision("deferred-no-resource", resource, version, logger);
        return false;
    }
    if (resource == g_uploadedResource && version == g_uploadedVersion)
    {
        LogDiagnosticDecision("skipped-already-uploaded", resource, version, logger);
        return true;
    }
    if (version == g_failedVersion)
    {
        LogDiagnosticDecision("skipped-known-failure", resource, version, logger);
        return false;
    }

    LogDiagnosticDecision("begin", resource, version, logger);

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
    LogDiagnosticDecision(ok ? "complete" : "failed", resource, version, logger);
    return ok;
}
} // namespace JacketUploadPipeline
