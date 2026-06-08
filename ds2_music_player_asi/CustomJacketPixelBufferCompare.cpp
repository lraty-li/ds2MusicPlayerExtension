#include "pch.h"

#include "CustomJacketInternal.h"

#include <sstream>

namespace
{
LONG g_logged = 0;

struct Span
{
    uint64_t base = 0;
    uint64_t size = 0;
};

struct VqInfo
{
    bool ok = false;
    uint64_t base = 0;
    uint64_t size = 0;
    DWORD protect = 0;
    DWORD type = 0;
    DWORD state = 0;
};

std::string H(uint64_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << value;
    return oss.str();
}

bool IsReadableProtect(DWORD protect)
{
    if (protect & PAGE_GUARD) return false;
    return protect != PAGE_NOACCESS && protect != 0;
}

VqInfo Query(uint64_t addr)
{
    VqInfo out = {};
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!addr || !VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)))
    {
        return out;
    }
    out.ok = true;
    out.base = reinterpret_cast<uint64_t>(mbi.BaseAddress);
    out.size = static_cast<uint64_t>(mbi.RegionSize);
    out.protect = mbi.Protect;
    out.type = mbi.Type;
    out.state = mbi.State;
    return out;
}

uint64_t ReadableSpan(uint64_t addr)
{
    const VqInfo vq = Query(addr);
    if (!vq.ok || vq.state != MEM_COMMIT || !IsReadableProtect(vq.protect))
    {
        return 0;
    }
    return vq.size - (addr - vq.base);
}

bool Contains(const Span& span, uint64_t ptr)
{
    return span.base && span.size && ptr >= span.base && ptr < span.base + span.size;
}

bool Read64(uint64_t addr, uint64_t& out)
{
    __try
    {
        out = *reinterpret_cast<uint64_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out = 0;
        return false;
    }
}

const char* Classify(uint64_t ptr, const Span& hot,
    const Span& noData, const Span& clone)
{
    if (!ptr) return "null";
    if (Contains(hot, ptr)) return "hotPB";
    if (Contains(noData, ptr)) return "noDataPB";
    if (Contains(clone, ptr)) return "clonePB";

    const VqInfo vq = Query(ptr);
    if (!vq.ok || vq.state != MEM_COMMIT || !IsReadableProtect(vq.protect))
    {
        return "unreadable";
    }
    if (vq.type == MEM_IMAGE) return "image";
    return "heap";
}

void LogVq(const char* label, uint64_t addr, const Logger& logger)
{
    const VqInfo vq = Query(addr);
    std::ostringstream oss;
    oss << "pbcmp vq " << label << " addr=" << H(addr);
    if (!vq.ok)
    {
        oss << " query=fail";
        logger.Log(oss.str());
        return;
    }
    oss << " base=" << H(vq.base)
        << " region=" << vq.size
        << " protect=" << H(vq.protect)
        << " type=" << H(vq.type)
        << " state=" << H(vq.state);
    logger.Log(oss.str());
}

void LogRootQwords(const char* label, uint64_t base, const Logger& logger)
{
    for (uint64_t rel = 0; rel <= 0x100; rel += 0x20)
    {
        uint64_t q[4] = {};
        for (uint32_t i = 0; i < 4; ++i)
        {
            Read64(base + rel + i * 8, q[i]);
        }
        std::ostringstream oss;
        oss << "pbcmp root " << label << "+" << H(rel) << ": "
            << H(q[0]) << " " << H(q[1]) << " "
            << H(q[2]) << " " << H(q[3]);
        logger.Log(oss.str());
    }
}

void LogField(const char* label, uint64_t owner, uint64_t field,
    const Span& hot, const Span& noData, const Span& clone, const Logger& logger)
{
    uint64_t ptr = 0;
    if (!Read64(owner + field, ptr))
    {
        std::ostringstream fail;
        fail << "pbcmp ptr " << label << " field=" << H(field) << " read=fail";
        logger.Log(fail.str());
        return;
    }

    const VqInfo vq = Query(ptr);
    std::ostringstream oss;
    oss << "pbcmp ptr " << label
        << " field=" << H(field)
        << " ptr=" << H(ptr)
        << " class=" << Classify(ptr, hot, noData, clone);
    if (vq.ok)
    {
        oss << " vqBase=" << H(vq.base)
            << " vqSize=" << vq.size
            << " prot=" << H(vq.protect)
            << " type=" << H(vq.type)
            << " state=" << H(vq.state);
    }
    logger.Log(oss.str());
}

void LogFields(const char* label, uint64_t owner,
    const Span& hot, const Span& noData, const Span& clone, const Logger& logger)
{
    const uint64_t fields[] = { 0x38, 0x88, 0x90, 0xC8, 0xD8, 0xE0 };
    for (uint32_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
    {
        LogField(label, owner, fields[i], hot, noData, clone, logger);
    }
}

bool ScanCloneRefs(uint64_t clonePB, uint64_t cloneSize,
    const Span& hot, uint32_t step, uint32_t& count, uint64_t* hits)
{
    count = 0;
    const uint64_t limit = cloneSize < 0x1000000ull ? cloneSize : 0x1000000ull;
    __try
    {
        auto* bytes = reinterpret_cast<uint8_t*>(clonePB);
        for (uint64_t off = 0; off + 8 <= limit; off += step)
        {
            const uint64_t value = *reinterpret_cast<uint64_t*>(bytes + off);
            if (!Contains(hot, value)) continue;
            if (count < 8) hits[count] = off;
            ++count;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return true;
}

void LogCloneRefs(uint64_t clonePB, uint64_t cloneSize,
    const Span& hot, const Logger& logger)
{
    uint64_t alignedHits[8] = {};
    uint64_t anyHits[8] = {};
    uint32_t alignedCount = 0;
    uint32_t anyCount = 0;
    const bool alignedOk = ScanCloneRefs(clonePB, cloneSize, hot, 8,
        alignedCount, alignedHits);
    const bool anyOk = ScanCloneRefs(clonePB, cloneSize, hot, 1,
        anyCount, anyHits);

    std::ostringstream oss;
    oss << "pbcmp clone refs-to-hot alignedOk=" << alignedOk
        << " aligned=" << alignedCount
        << " anyOk=" << anyOk
        << " any=" << anyCount;
    for (uint32_t i = 0; i < alignedCount && i < 4; ++i)
    {
        oss << " a" << i << "=" << H(alignedHits[i]);
    }
    for (uint32_t i = 0; i < anyCount && i < 4; ++i)
    {
        oss << " x" << i << "=" << H(anyHits[i]);
    }
    logger.Log(oss.str());
}
} // namespace

namespace CustomJacketInternal
{
void DumpPixelBufferComparisonOnce(uint64_t hotPB, uint64_t noDataPB,
    uint64_t clonePB, uint64_t cloneSize, const Logger& logger)
{
    if (!hotPB || !noDataPB || !clonePB) return;
    if (InterlockedExchange(&g_logged, 1) != 0) return;

    const Span hot = { hotPB, ReadableSpan(hotPB) };
    const Span noData = { noDataPB, ReadableSpan(noDataPB) };
    const Span clone = { clonePB, cloneSize };

    std::ostringstream intro;
    intro << "pbcmp begin hot=" << H(hotPB)
        << " hotSize=" << hot.size
        << " noData=" << H(noDataPB)
        << " noDataSize=" << noData.size
        << " clone=" << H(clonePB)
        << " cloneSize=" << clone.size;
    logger.Log(intro.str());

    LogVq("hot", hotPB, logger);
    LogVq("noData", noDataPB, logger);
    LogVq("clone", clonePB, logger);
    LogRootQwords("hot", hotPB, logger);
    LogRootQwords("noData", noDataPB, logger);
    LogRootQwords("clone", clonePB, logger);
    LogFields("hot", hotPB, hot, noData, clone, logger);
    LogFields("noData", noDataPB, hot, noData, clone, logger);
    LogFields("clone", clonePB, hot, noData, clone, logger);
    LogCloneRefs(clonePB, cloneSize, hot, logger);
}
} // namespace CustomJacketInternal
