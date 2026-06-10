#include "pch.h"

#include "CustomJacketInternal.h"

#include <sstream>

namespace
{
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
        logger.Log("uiclone TextureDX12 ext38: source block too small");
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
    oss << "uiclone TextureDX12 ext38 clone: srcBlock=0x" << std::hex << block
        << " newBlock=0x" << reinterpret_cast<uint64_t>(copy)
        << std::dec << " size=" << blockSize
        << " relocatedTextureRefs=" << relocated
        << " relocatedSelfRefs=" << selfRelocated;
    logger.Log(oss.str());
    return reinterpret_cast<uint64_t>(copy);
}
} // namespace

namespace CustomJacketInternal
{
uint8_t* ClonePixelBufferForUiClone(uint64_t source,
    uint64_t& cloneSize, uint32_t& relocated, uint32_t& relocatedExt38,
    const Logger& logger)
{
    cloneSize = ReadableBytes(source);
    relocated = 0;
    relocatedExt38 = 0;
    if (cloneSize < 0x10000)
    {
        logger.Log("uiclone: source TextureDX12 readable too small");
        return nullptr;
    }

    auto* copy = static_cast<uint8_t*>(VirtualAlloc(nullptr,
        static_cast<size_t>(cloneSize), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!copy)
    {
        logger.Log("uiclone: VirtualAlloc TextureDX12 failed");
        return nullptr;
    }
    if (!SehMemcpySafe(copy, reinterpret_cast<void*>(source), static_cast<size_t>(cloneSize)))
    {
        logger.Log("uiclone: memcpy TextureDX12 failed");
        VirtualFree(copy, 0, MEM_RELEASE);
        return nullptr;
    }

    relocated = RelocateInternalPointers(copy, source, cloneSize);
    CloneExternalBlock(copy, source, cloneSize, relocatedExt38, logger);
    return copy;
}
} // namespace CustomJacketInternal
