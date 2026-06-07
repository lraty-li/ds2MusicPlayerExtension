#include "pch.h"

#include "CustomJacketInternal.h"

#include <sstream>

namespace
{
LONG g_trackDumped = 0;

std::string H64(uint64_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << value;
    return oss.str();
}

std::string H32(uint32_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << value;
    return oss.str();
}

bool Read32(uint64_t addr, uint32_t& out)
{
    __try
    {
        out = *reinterpret_cast<uint32_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out = 0;
        return false;
    }
}

void LogQwords(const char* label, uint64_t base, uint32_t bytes, const Logger& logger)
{
    for (uint32_t rel = 0; rel < bytes; rel += 0x20)
    {
        uint64_t q[4] = {};
        for (uint32_t i = 0; i < 4; ++i)
        {
            CustomJacketInternal::SehReadU64(base + rel + i * 8, q[i]);
        }

        std::ostringstream oss;
        oss << "jres " << label << "+0x" << std::hex << rel << ": "
            << H64(q[0]) << " " << H64(q[1]) << " "
            << H64(q[2]) << " " << H64(q[3]);
        logger.Log(oss.str());
    }
}

void LogDwordRange(const char* label, uint64_t base, uint32_t bytes, const Logger& logger)
{
    for (uint32_t rel = 0; rel < bytes; rel += 0x20)
    {
        std::ostringstream oss;
        oss << "jres " << label << " d+0x" << std::hex << rel << ": ";
        for (uint32_t i = 0; i < 8; ++i)
        {
            uint32_t value = 0;
            if (i) oss << " ";
            oss << (Read32(base + rel + i * 4, value) ? H32(value) : "read_failed");
        }
        logger.Log(oss.str());
    }
}

void LogSlotPointer(const char* label, uint64_t slotPtr, const Logger& logger)
{
    CustomJacketSlot slot = {};
    std::ostringstream oss;
    oss << "jres " << label << " slotPtr=" << H64(slotPtr);
    if (slotPtr && CustomJacketInternal::SehReadSlot(slotPtr, slot))
    {
        oss << " target=" << H64(slot.target)
            << " packed=" << H64(slot.packed);
    }
    else
    {
        oss << " unreadable";
    }
    logger.Log(oss.str());
}
} // namespace

namespace CustomJacketInternal
{
void ResetTrackAlbumProbeDiagnostics()
{
    InterlockedExchange(&g_trackDumped, 0);
}

void DumpTrackAlbumProbeOnce(void* track, const Logger& logger)
{
    if (!track || InterlockedExchange(&g_trackDumped, 1))
    {
        return;
    }

    const uint64_t trackBase = reinterpret_cast<uint64_t>(track);
    uint64_t album = 0;
    uint64_t title = 0;
    uint64_t jacketSlot = 0;
    SehReadU64(trackBase + 0x30, album);
    SehReadU64(trackBase + 0x38, title);
    SehReadU64(trackBase + 0x50, jacketSlot);

    std::ostringstream intro;
    intro << "jres track=" << H64(trackBase)
        << " album=" << H64(album)
        << " title=" << H64(title)
        << " jacketSlot=" << H64(jacketSlot);
    logger.Log(intro.str());

    LogQwords("track", trackBase, 0xC0, logger);
    LogDwordRange("track", trackBase, 0xC0, logger);
    LogSlotPointer("TrackJacketSlot", jacketSlot, logger);

    if (album)
    {
        LogQwords("album", album, 0x80, logger);
        LogDwordRange("album", album, 0x80, logger);
    }
}
} // namespace CustomJacketInternal
