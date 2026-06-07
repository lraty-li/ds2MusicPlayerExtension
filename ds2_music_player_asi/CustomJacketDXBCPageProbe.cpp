#include "pch.h"

#include "CustomJacketInternal.h"

#include <sstream>

namespace
{
constexpr uint32_t kDxbcFourcc = 0x43425844;
constexpr uint64_t kMaxPixelBufferProbe = 0x1000000;
LONG g_headerDumpMask = 0;

bool FindDXBCMarker(uint8_t* page, uint32_t& markerRel)
{
    __try
    {
        for (uint32_t rel = 0x40; rel <= 0x70; ++rel)
        {
            if (*reinterpret_cast<uint32_t*>(page + rel) == kDxbcFourcc)
            {
                markerRel = rel;
                return true;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return false;
}

bool ReadPageHeader(uint8_t* page, uint32_t& markerRel,
    uint32_t& dataSize, uint32_t& mipCount)
{
    __try
    {
        if (!FindDXBCMarker(page, markerRel)) return false;
        dataSize = *reinterpret_cast<uint32_t*>(page + markerRel + 0x18);
        mipCount = *reinterpret_cast<uint8_t*>(page + markerRel + 0x1C);
        return dataSize > 0 && dataSize < kMaxPixelBufferProbe && mipCount > 0 && mipCount <= 16;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

uint32_t MipOffsetTableRel(uint32_t markerRel)
{
    return markerRel + 0x20;
}

uint32_t HeaderDataStart(uint32_t markerRel, uint32_t mipCount)
{
    return (MipOffsetTableRel(markerRel) + mipCount * 4 + 15) & ~15u;
}

uint32_t ReadFirstMipOffset(uint8_t* page, uint32_t markerRel)
{
    __try
    {
        return *reinterpret_cast<uint32_t*>(page + MipOffsetTableRel(markerRel));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

uint32_t FindPayloadDXBC(uint8_t* page, uint32_t dataStart, uint32_t dataSize)
{
    __try
    {
        const uint32_t end = dataStart + dataSize;
        for (uint32_t rel = dataStart; rel + 4 <= end; ++rel)
        {
            if (*reinterpret_cast<uint32_t*>(page + rel) == kDxbcFourcc)
            {
                return rel;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    return 0;
}

LONG HeaderDumpBitForSize(uint64_t readable)
{
    if (readable >= 0x800000) return 8;
    if (readable >= 0x400000) return 4;
    if (readable >= 0x100000) return 2;
    if (readable >= 0x40000) return 1;
    return 1;
}

void AppendMipOffsets(std::ostringstream& oss, uint8_t* page,
    uint32_t markerRel, uint32_t mipCount)
{
    oss << " mipOffs=";
    const uint32_t tableRel = MipOffsetTableRel(markerRel);
    const uint32_t count = mipCount < 8 ? mipCount : 8;
    __try
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            if (i) oss << ",";
            oss << "0x" << std::hex
                << *reinterpret_cast<uint32_t*>(page + tableRel + i * 4);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        oss << "read_failed";
    }
}
} // namespace

namespace CustomJacketInternal
{
void ResetDXBCPageHeaderDiagnostics()
{
    InterlockedExchange(&g_headerDumpMask, 0);
}

void DumpDXBCPageHeadersOnce(uint64_t pbAddr,
    const CustomJacketPixelBufferInfo& info, const Logger& logger)
{
    const LONG bit = HeaderDumpBitForSize(info.readableSize);
    const LONG oldMask = InterlockedOr(&g_headerDumpMask, bit);
    if (oldMask & bit) return;

    uint32_t logged = 0;
    auto* base = reinterpret_cast<uint8_t*>(pbAddr);
    for (uint64_t off = 0x10000; off + 0x80 < info.readableSize && logged < 12; off += 0x1000)
    {
        uint32_t markerRel = 0;
        uint32_t dataSize = 0;
        uint32_t mipCount = 0;
        if (!ReadPageHeader(base + off, markerRel, dataSize, mipCount)) continue;

        const uint32_t firstMipOffset = ReadFirstMipOffset(base + off, markerRel);
        const uint32_t tableEnd = MipOffsetTableRel(markerRel) + mipCount * 4;
        const uint32_t alignedStart = HeaderDataStart(markerRel, mipCount);
        const uint32_t firstAbs = markerRel + firstMipOffset;
        const uint64_t pageEnd = off + firstAbs + dataSize;
        const uint32_t payloadDXBC = FindPayloadDXBC(base + off, firstAbs, dataSize);

        std::ostringstream oss;
        oss << "bcn page header off=0x" << std::hex << off
            << " marker=0x" << markerRel
            << " table=0x" << MipOffsetTableRel(markerRel)
            << " tableEnd=0x" << tableEnd
            << " firstAbs=0x" << firstAbs
            << " alignedStart=0x" << alignedStart
            << std::dec << " dataSize=" << dataSize
            << " mips=" << mipCount
            << " pageEnd=0x" << std::hex << pageEnd
            << " payloadDXBC=0x" << payloadDXBC;
        AppendMipOffsets(oss, base + off, markerRel, mipCount);
        logger.Log(oss.str());
        ++logged;
    }
}
} // namespace CustomJacketInternal
