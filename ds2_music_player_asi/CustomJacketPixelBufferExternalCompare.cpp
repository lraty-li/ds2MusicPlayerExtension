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

bool ReadField(uint64_t owner, uint64_t field, uint64_t& ptr)
{
    return Read64(owner + field, ptr) && ptr;
}

void LogBlockIntro(const char* label, uint64_t field,
    uint64_t ptr, const Logger& logger)
{
    const VqInfo vq = Query(ptr);
    std::ostringstream oss;
    oss << "pbext block " << label
        << " field=" << H(field)
        << " ptr=" << H(ptr);
    if (vq.ok)
    {
        oss << " base=" << H(vq.base)
            << " region=" << vq.size
            << " protect=" << H(vq.protect)
            << " type=" << H(vq.type)
            << " state=" << H(vq.state);
    }
    else
    {
        oss << " query=fail";
    }
    logger.Log(oss.str());
}

void LogQwords(const char* label, uint64_t field,
    uint64_t ptr, const Logger& logger)
{
    for (uint64_t rel = 0; rel <= 0x100; rel += 0x20)
    {
        uint64_t q[4] = {};
        for (uint32_t i = 0; i < 4; ++i)
        {
            Read64(ptr + rel + i * 8, q[i]);
        }
        std::ostringstream oss;
        oss << "pbext q " << label
            << " field=" << H(field)
            << " +" << H(rel) << ": "
            << H(q[0]) << " " << H(q[1]) << " "
            << H(q[2]) << " " << H(q[3]);
        logger.Log(oss.str());
    }
}

void LogPointerClasses(const char* label, uint64_t field, uint64_t ptr,
    const Span& hot, const Span& noData, const Span& clone, const Logger& logger)
{
    for (uint64_t rel = 0; rel <= 0x100; rel += 8)
    {
        uint64_t value = 0;
        if (!Read64(ptr + rel, value) || value < 0x100000000ull) continue;
        const char* cls = Classify(value, hot, noData, clone);
        if (strcmp(cls, "unreadable") == 0) continue;

        std::ostringstream oss;
        oss << "pbext ptr " << label
            << " field=" << H(field)
            << " off=" << H(rel)
            << " value=" << H(value)
            << " class=" << cls;
        logger.Log(oss.str());
    }
}

uint32_t CountRefs(uint64_t ptr, uint64_t size, const Span& span,
    uint64_t* hits)
{
    uint32_t count = 0;
    const uint64_t limit = size < 0x4000 ? size : 0x4000;
    __try
    {
        auto* bytes = reinterpret_cast<uint8_t*>(ptr);
        for (uint64_t off = 0; off + 8 <= limit; off += 8)
        {
            const uint64_t value = *reinterpret_cast<uint64_t*>(bytes + off);
            if (!Contains(span, value)) continue;
            if (count < 4) hits[count] = off;
            ++count;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return count;
    }
    return count;
}

void LogRefCounts(const char* label, uint64_t field, uint64_t ptr,
    const Span& hot, const Span& noData, const Span& clone, const Logger& logger)
{
    const uint64_t size = ReadableSpan(ptr);
    uint64_t hotHits[4] = {};
    uint64_t noDataHits[4] = {};
    uint64_t cloneHits[4] = {};
    const uint32_t hotCount = CountRefs(ptr, size, hot, hotHits);
    const uint32_t noDataCount = CountRefs(ptr, size, noData, noDataHits);
    const uint32_t cloneCount = CountRefs(ptr, size, clone, cloneHits);

    std::ostringstream oss;
    oss << "pbext refs " << label
        << " field=" << H(field)
        << " size=" << size
        << " hot=" << hotCount
        << " noData=" << noDataCount
        << " clone=" << cloneCount;
    if (hotCount) oss << " hot0=" << H(hotHits[0]);
    if (noDataCount) oss << " noData0=" << H(noDataHits[0]);
    if (cloneCount) oss << " clone0=" << H(cloneHits[0]);
    logger.Log(oss.str());
}

void DumpOwnerField(const char* label, uint64_t owner, uint64_t field,
    const Span& hot, const Span& noData, const Span& clone, const Logger& logger)
{
    uint64_t ptr = 0;
    if (!ReadField(owner, field, ptr)) return;
    LogBlockIntro(label, field, ptr, logger);
    LogQwords(label, field, ptr, logger);
    LogPointerClasses(label, field, ptr, hot, noData, clone, logger);
    LogRefCounts(label, field, ptr, hot, noData, clone, logger);
}
} // namespace

namespace CustomJacketInternal
{
void DumpPixelBufferExternalBlocksOnce(uint64_t hotPB, uint64_t noDataPB,
    uint64_t clonePB, uint64_t cloneSize, const Logger& logger)
{
    if (!hotPB || !noDataPB || !clonePB) return;
    if (InterlockedExchange(&g_logged, 1) != 0) return;

    const Span hot = { hotPB, ReadableSpan(hotPB) };
    const Span noData = { noDataPB, ReadableSpan(noDataPB) };
    const Span clone = { clonePB, cloneSize };
    const uint64_t fields[] = { 0x38, 0x90, 0xE0 };

    logger.Log("pbext begin external PB block comparison");
    for (uint32_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
    {
        DumpOwnerField("hot", hotPB, fields[i], hot, noData, clone, logger);
        DumpOwnerField("noData", noDataPB, fields[i], hot, noData, clone, logger);
        DumpOwnerField("clone", clonePB, fields[i], hot, noData, clone, logger);
    }
}
} // namespace CustomJacketInternal
