#include "pch.h"

#include "CustomJacketInternal.h"

#include <sstream>

namespace
{
constexpr uint32_t kDxbcFourcc = 0x43425844;

bool IsReadableProtect(DWORD protect)
{
    if (protect & PAGE_GUARD) return false;
    if (protect == PAGE_NOACCESS) return false;
    return protect != 0;
}

uint64_t ReadableBytes(uint64_t addr)
{
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT || !IsReadableProtect(mbi.Protect)) return 0;
    const uint64_t base = reinterpret_cast<uint64_t>(mbi.BaseAddress);
    const uint64_t available = static_cast<uint64_t>(mbi.RegionSize) - (addr - base);
    return available < 0x1000000ull ? available : 0x1000000ull;
}

uint32_t RelocateInternalPointers(uint8_t* copy, uint64_t source, uint64_t size)
{
    uint32_t count = 0;
    __try
    {
        for (uint64_t off = 0; off + 8 <= size; off += 8)
        {
            auto* q = reinterpret_cast<uint64_t*>(copy + off);
            if (*q < source || *q >= source + size) continue;
            *q = reinterpret_cast<uint64_t>(copy) + (*q - source);
            ++count;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return count;
    }
    return count;
}

uint32_t RelocatePointerRange(uint8_t* copy, uint64_t size,
    uint64_t oldBase, uint64_t oldSize, uint64_t newBase)
{
    uint32_t count = 0;
    __try
    {
        for (uint64_t off = 0; off + 8 <= size; off += 8)
        {
            auto* q = reinterpret_cast<uint64_t*>(copy + off);
            if (*q < oldBase || *q >= oldBase + oldSize) continue;
            *q = newBase + (*q - oldBase);
            ++count;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return count;
    }
    return count;
}

uint64_t CloneExternalBlock(uint8_t* pbCopy, uint64_t sourcePB,
    uint64_t cloneSize, uint32_t& relocated, const Logger& logger)
{
    uint64_t block = 0;
    if (!CustomJacketInternal::SehReadU64(sourcePB + 0x38, block) || !block)
    {
        return 0;
    }

    const uint64_t blockSize = ReadableBytes(block);
    if (blockSize < 0x120)
    {
        logger.Log("uiclone PB ext38: source block too small");
        return 0;
    }

    auto* copy = static_cast<uint8_t*>(VirtualAlloc(nullptr,
        static_cast<size_t>(blockSize), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!copy) return 0;
    if (!CustomJacketInternal::SehMemcpySafe(copy,
        reinterpret_cast<void*>(block), static_cast<size_t>(blockSize)))
    {
        VirtualFree(copy, 0, MEM_RELEASE);
        return 0;
    }

    const uint64_t cloneBase = reinterpret_cast<uint64_t>(pbCopy);
    relocated = RelocatePointerRange(copy, blockSize, sourcePB, cloneSize, cloneBase);
    const uint32_t selfRelocated = RelocatePointerRange(copy, blockSize,
        block, blockSize, reinterpret_cast<uint64_t>(copy));
    *reinterpret_cast<uint64_t*>(pbCopy + 0x38) = reinterpret_cast<uint64_t>(copy);

    std::ostringstream oss;
    oss << "uiclone PB ext38 clone: srcBlock=0x" << std::hex << block
        << " newBlock=0x" << reinterpret_cast<uint64_t>(copy)
        << std::dec << " size=" << blockSize
        << " relocatedPBRefs=" << relocated
        << " relocatedSelfRefs=" << selfRelocated;
    logger.Log(oss.str());
    return reinterpret_cast<uint64_t>(copy);
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

struct PatchPage
{
    uint64_t off = 0;
    uint32_t markerRel = 0;
    uint32_t payloadRel = 0;
    uint32_t dataSize = 0;
    uint32_t writeBytes = 0;
    uint8_t mipCount = 0;
};

bool ReadDXBCPage(uint8_t* page, PatchPage& out)
{
    __try
    {
        uint32_t markerRel = 0;
        if (!FindDXBCMarker(page, markerRel)) return false;

        const uint32_t dataSize = *reinterpret_cast<uint32_t*>(page + markerRel + 0x18);
        const uint32_t mips = *reinterpret_cast<uint8_t*>(page + markerRel + 0x1C);
        if (!dataSize || dataSize > 0x100000 || !mips || mips > 16) return false;

        const uint32_t firstMipOffset = *reinterpret_cast<uint32_t*>(page + markerRel + 0x20);
        const uint32_t payloadRel = markerRel + firstMipOffset;
        const uint32_t tableEnd = markerRel + 0x20 + mips * 4;
        if (payloadRel < tableEnd || payloadRel >= 0x10000) return false;

        out.markerRel = markerRel;
        out.payloadRel = payloadRel;
        out.dataSize = dataSize;
        out.writeBytes = dataSize;
        out.mipCount = static_cast<uint8_t>(mips);
        if (out.writeBytes > 0x10000 - payloadRel)
        {
            out.writeBytes = 0x10000 - payloadRel;
        }
        return out.writeBytes >= 16;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void FillTestBlock(uint8_t* dst, uint32_t seed)
{
    const uint8_t colors[][16] = {
        {0xFF,0x00,0,0,0,0,0,0, 0x00,0xF8,0x1F,0x00,0,0,0,0},
        {0xFF,0x00,0,0,0,0,0,0, 0xE0,0x07,0x00,0x00,0,0,0,0},
        {0xFF,0x00,0,0,0,0,0,0, 0x1F,0x00,0x00,0xF8,0,0,0,0},
        {0xFF,0x00,0,0,0,0,0,0, 0xE0,0xFF,0x1F,0x00,0,0,0,0},
    };
    memcpy(dst, colors[seed % 4], 16);
}

void LogPatchPage(const PatchPage& page, uint32_t index, const Logger& logger)
{
    if (index >= 8) return;

    std::ostringstream oss;
    oss << "uiclone PB patch page off=0x" << std::hex << page.off
        << " marker=0x" << page.markerRel
        << " payload=0x" << page.payloadRel
        << std::dec << " dataSize=" << page.dataSize
        << " writeBytes=" << page.writeBytes
        << " mips=" << static_cast<uint32_t>(page.mipCount);
    logger.Log(oss.str());
}

uint32_t PatchDXBCPayloads(uint8_t* buffer, uint64_t size,
    uint32_t& bytesWritten, const Logger& logger)
{
    uint32_t pages = 0;
    bytesWritten = 0;
    __try
    {
        for (uint64_t off = 0x10000; off + 0x10000 <= size; off += 0x1000)
        {
            PatchPage page = {};
            if (!ReadDXBCPage(buffer + off, page)) continue;
            if (off + page.payloadRel + page.writeBytes > size) continue;

            page.off = off;
            LogPatchPage(page, pages, logger);
            for (uint32_t rel = 0; rel + 16 <= page.writeBytes; rel += 16)
            {
                FillTestBlock(buffer + off + page.payloadRel + rel, pages + rel / 16);
            }
            ++pages;
            bytesWritten += page.writeBytes;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return pages;
    }
    return pages;
}
} // namespace

namespace CustomJacketInternal
{
uint8_t* CloneAndPatchPixelBufferForUiClone(uint64_t source,
    uint64_t& cloneSize, uint32_t& relocated, uint32_t& patchedPages, const Logger& logger)
{
    cloneSize = ReadableBytes(source);
    relocated = 0;
    patchedPages = 0;
    if (cloneSize < 0x10000)
    {
        logger.Log("uiclone: source pixelBuffer readable too small");
        return nullptr;
    }

    auto* copy = static_cast<uint8_t*>(VirtualAlloc(nullptr,
        static_cast<size_t>(cloneSize), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!copy)
    {
        logger.Log("uiclone: VirtualAlloc pixelBuffer failed");
        return nullptr;
    }
    if (!SehMemcpySafe(copy, reinterpret_cast<void*>(source), static_cast<size_t>(cloneSize)))
    {
        logger.Log("uiclone: memcpy pixelBuffer failed");
        VirtualFree(copy, 0, MEM_RELEASE);
        return nullptr;
    }

    relocated = RelocateInternalPointers(copy, source, cloneSize);
    uint32_t extRelocated = 0;
    CloneExternalBlock(copy, source, cloneSize, extRelocated, logger);
    uint32_t patchedBytes = 0;
    patchedPages = PatchDXBCPayloads(copy, cloneSize, patchedBytes, logger);

    std::ostringstream oss;
    oss << "uiclone PB patch: pages=" << patchedPages
        << " bytes=" << patchedBytes;
    logger.Log(oss.str());
    return copy;
}
} // namespace CustomJacketInternal
