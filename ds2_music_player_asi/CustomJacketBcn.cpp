#include "pch.h"

#include "CustomJacketInternal.h"

#include <sstream>

namespace
{
constexpr uint32_t kDxbcFourcc = 0x43425844;
constexpr uint64_t kMaxPixelBufferProbe = 0x1000000;

uint64_t AlignUp64K(uint64_t value)
{
    return (value + 0xFFFFull) & ~0xFFFFull;
}

bool IsReadableProtect(DWORD protect)
{
    if (protect & PAGE_GUARD) return false;
    if (protect == PAGE_NOACCESS) return false;
    return protect != 0;
}

uint64_t ReadableBytesFrom(uint64_t addr)
{
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)))
    {
        return 0;
    }
    if (mbi.State != MEM_COMMIT || !IsReadableProtect(mbi.Protect))
    {
        return 0;
    }

    const auto base = reinterpret_cast<uint64_t>(mbi.BaseAddress);
    const uint64_t offset = addr - base;
    const uint64_t available = static_cast<uint64_t>(mbi.RegionSize) - offset;
    return available < kMaxPixelBufferProbe ? available : kMaxPixelBufferProbe;
}

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

uint32_t PayloadStart(uint8_t* page, uint32_t markerRel)
{
    return markerRel + ReadFirstMipOffset(page, markerRel);
}

} // namespace

namespace CustomJacketInternal
{
bool ProbePixelBuffer(uint64_t pbAddr, CustomJacketPixelBufferInfo& out)
{
    out = {};
    const uint64_t readable = ReadableBytesFrom(pbAddr);
    if (readable < 0x10000) return false;

    __try
    {
        out.pageTableWidth = *reinterpret_cast<uint32_t*>(pbAddr + 0x30);
        out.pageTableHeight = *reinterpret_cast<uint32_t*>(pbAddr + 0x34);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    uint64_t maxSeen = 0x10000;
    auto* base = reinterpret_cast<uint8_t*>(pbAddr);
    for (uint64_t off = 0x10000; off + 0x70 < readable; off += 0x1000)
    {
        uint32_t dataSize = 0;
        uint32_t mipCount = 0;
        uint32_t markerRel = 0;
        if (FindDXBCMarker(base + off, markerRel))
        {
            ++out.dxbcMarkers;
        }
        if (!ReadPageHeader(base + off, markerRel, dataSize, mipCount)) continue;

        ++out.dxbcPages;
        const uint32_t payloadStart = PayloadStart(base + off, markerRel);
        const uint64_t pageEnd = off + payloadStart + dataSize;
        maxSeen = pageEnd > maxSeen ? pageEnd : maxSeen;
    }

    out.readableSize = readable;
    out.cloneSize = AlignUp64K(maxSeen);
    if (out.cloneSize > readable) out.cloneSize = readable;
    return true;
}

int OverwriteDXBCPages(uint8_t* pixelBuffer, size_t sizeBytes, const Logger& logger)
{
    UNREFERENCED_PARAMETER(pixelBuffer);
    UNREFERENCED_PARAMETER(sizeBytes);
    UNREFERENCED_PARAMETER(logger);
    return 0;
}
} // namespace CustomJacketInternal
