#include "pch.h"

#include "CustomJacketInternal.h"

#include <sstream>

namespace
{
constexpr uint32_t kDxbc = 0x43425844;
constexpr uint32_t kDds = 0x20534444;

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

uint64_t ReadableBytes(uint64_t addr)
{
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT || mbi.Protect == PAGE_NOACCESS) return 0;
    if (mbi.Protect & PAGE_GUARD) return 0;
    const auto base = reinterpret_cast<uint64_t>(mbi.BaseAddress);
    return static_cast<uint64_t>(mbi.RegionSize) - (addr - base);
}

bool ScanMarker(uint64_t base, uint64_t size, uint32_t marker,
    uint32_t& count, uint64_t* hits)
{
    const uint64_t limit = size < 0x100000 ? size : 0x100000;
    count = 0;
    __try
    {
        auto* bytes = reinterpret_cast<uint8_t*>(base);
        for (uint64_t off = 0; off + 4 <= limit; ++off)
        {
            if (*reinterpret_cast<uint32_t*>(bytes + off) != marker) continue;
            if (count < 3) hits[count] = off;
            ++count;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return true;
}

void LogNestedPointer(uint64_t field, uint64_t offset, uint64_t ptr, const Logger& logger)
{
    const uint64_t readable = ReadableBytes(ptr);
    if (!readable) return;

    uint64_t q0 = 0, q1 = 0;
    uint32_t d48 = 0, d50 = 0;
    Read64(ptr, q0);
    Read64(ptr + 8, q1);
    Read32(ptr + 0x48, d48);
    Read32(ptr + 0x50, d50);

    std::ostringstream oss;
    oss << "pb nested field=" << H64(field)
        << " off=" << H64(offset)
        << " ptr=" << H64(ptr)
        << " readable=" << readable
        << " q0=" << H64(q0)
        << " q1=" << H64(q1)
        << " d48=" << H32(d48)
        << " d50=" << H32(d50);
    logger.Log(oss.str());
}

void LogNestedPointers(uint64_t field, uint64_t owner, const Logger& logger)
{
    uint32_t logged = 0;
    for (uint64_t off = 0; off <= 0x60 && logged < 5; off += 8)
    {
        uint64_t ptr = 0;
        if (!Read64(owner + off, ptr) || ptr < 0x100000000ull) continue;
        if (!ReadableBytes(ptr)) continue;
        LogNestedPointer(field, off, ptr, logger);
        ++logged;
    }
}

void LogPointerBlock(uint64_t owner, uint64_t field, const Logger& logger)
{
    uint64_t ptr = 0;
    if (!Read64(owner + field, ptr) || !ptr) return;

    const uint64_t readable = ReadableBytes(ptr);
    uint64_t q0 = 0, q1 = 0, q2 = 0, q3 = 0;
    uint32_t d48 = 0, d50 = 0;
    Read64(ptr, q0);
    Read64(ptr + 8, q1);
    Read64(ptr + 0x10, q2);
    Read64(ptr + 0x18, q3);
    Read32(ptr + 0x48, d48);
    Read32(ptr + 0x50, d50);

    std::ostringstream oss;
    oss << "pb ptr field=" << H64(field)
        << " ptr=" << H64(ptr)
        << " readable=" << readable
        << " q=" << H64(q0) << "," << H64(q1)
        << "," << H64(q2) << "," << H64(q3)
        << " d48=" << H32(d48)
        << " d50=" << H32(d50);
    logger.Log(oss.str());

    uint64_t hits[3] = {};
    uint32_t dxbc = 0, dds = 0;
    ScanMarker(ptr, readable, kDxbc, dxbc, hits);
    std::ostringstream m1;
    m1 << "pb ptr field=" << H64(field) << " DXBC count=" << dxbc;
    for (uint32_t i = 0; i < dxbc && i < 3; ++i) m1 << " hit" << i << "=" << H64(hits[i]);
    logger.Log(m1.str());

    memset(hits, 0, sizeof(hits));
    ScanMarker(ptr, readable, kDds, dds, hits);
    std::ostringstream m2;
    m2 << "pb ptr field=" << H64(field) << " DDS count=" << dds;
    for (uint32_t i = 0; i < dds && i < 3; ++i) m2 << " hit" << i << "=" << H64(hits[i]);
    logger.Log(m2.str());

    LogNestedPointers(field, ptr, logger);
}
} // namespace

namespace CustomJacketInternal
{
void DumpPixelBufferPointersOnce(uint64_t pbAddr, const Logger& logger)
{
    const uint64_t fields[] = { 0x38, 0x88, 0x90, 0xC8, 0xD8, 0xE0 };
    for (uint32_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
    {
        LogPointerBlock(pbAddr, fields[i], logger);
    }
}
} // namespace CustomJacketInternal
