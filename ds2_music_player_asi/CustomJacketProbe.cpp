#include "pch.h"

#include "CustomJacketInternal.h"

#include <sstream>

namespace
{
constexpr uint32_t kDxbc = 0x43425844;
constexpr uint32_t kDds = 0x20534444;
LONG g_dumpMask = 0;

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

void LogQwords(uint64_t base, uint64_t rel, const Logger& logger)
{
    uint64_t q[4] = {};
    for (uint32_t i = 0; i < 4; ++i)
    {
        Read64(base + rel + i * 8, q[i]);
    }

    std::ostringstream oss;
    oss << "pb q+" << H64(rel) << ": "
        << H64(q[0]) << " " << H64(q[1]) << " "
        << H64(q[2]) << " " << H64(q[3]);
    logger.Log(oss.str());
}

void LogPage(uint64_t base, uint64_t rel, uint64_t readable, const Logger& logger)
{
    if (rel + 0x58 >= readable) return;

    uint64_t q0 = 0, q1 = 0, q2 = 0;
    uint32_t d40 = 0, d48 = 0, d50 = 0, d51 = 0, d52 = 0;
    Read64(base + rel, q0);
    Read64(base + rel + 8, q1);
    Read64(base + rel + 0x10, q2);
    Read32(base + rel + 0x40, d40);
    Read32(base + rel + 0x48, d48);
    Read32(base + rel + 0x50, d50);
    Read32(base + rel + 0x51, d51);
    Read32(base + rel + 0x52, d52);

    std::ostringstream oss;
    oss << "pb page+" << H64(rel)
        << " q0=" << H64(q0)
        << " q1=" << H64(q1)
        << " q2=" << H64(q2)
        << " d40=" << H32(d40)
        << " d48=" << H32(d48)
        << " d50=" << H32(d50);
    oss << " d51=" << H32(d51)
        << " d52=" << H32(d52);
    logger.Log(oss.str());
}

bool SehScanMarker(uint64_t base, uint64_t size, uint32_t marker,
    uint64_t* hits, uint32_t& count)
{
    const uint64_t scanSize = size < 0x300000 ? size : 0x300000;
    count = 0;

    __try
    {
        auto* bytes = reinterpret_cast<uint8_t*>(base);
        for (uint64_t off = 0; off + 4 <= scanSize; ++off)
        {
            if (*reinterpret_cast<uint32_t*>(bytes + off) != marker) continue;
            if (count < 4) hits[count] = off;
            ++count;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return true;
}

void LogMarker(uint64_t base, uint64_t size, uint32_t marker,
    const char* name, const Logger& logger)
{
    uint64_t hits[4] = {};
    uint32_t count = 0;
    if (!SehScanMarker(base, size, marker, hits, count))
    {
        logger.Log(std::string("pb marker scan failed: ") + name);
        return;
    }

    std::ostringstream oss;
    oss << "pb marker " << name << " count=" << count;
    for (uint32_t i = 0; i < count && i < 4; ++i)
    {
        oss << " hit" << i << "=" << H64(hits[i]);
    }
    logger.Log(oss.str());
}

void LogDXBCHitContext(uint64_t base, uint64_t hit, uint64_t size, const Logger& logger)
{
    if (hit < 0x60 || hit + 0x30 >= size) return;

    uint64_t q0 = 0, q8 = 0;
    uint32_t dm40 = 0, dm10 = 0, d0 = 0, d17 = 0, d18 = 0, d1B = 0;
    Read64(base + hit - 0x50, q0);
    Read64(base + hit - 0x48, q8);
    Read32(base + hit - 0x40, dm40);
    Read32(base + hit - 0x10, dm10);
    Read32(base + hit, d0);
    Read32(base + hit + 0x17, d17);
    Read32(base + hit + 0x18, d18);
    Read32(base + hit + 0x1B, d1B);

    std::ostringstream oss;
    oss << "pb DXBC ctx hit=" << H64(hit)
        << " q[-50]=" << H64(q0)
        << " q[-48]=" << H64(q8)
        << " d[-40]=" << H32(dm40)
        << " d[-10]=" << H32(dm10)
        << " d[0]=" << H32(d0)
        << " d[17]=" << H32(d17)
        << " d[18]=" << H32(d18)
        << " d[1B]=" << H32(d1B);
    logger.Log(oss.str());
}
} // namespace

namespace CustomJacketInternal
{
void ResetPixelBufferDiagnostics()
{
    InterlockedExchange(&g_dumpMask, 0);
    ResetDXBCPageHeaderDiagnostics();
}

LONG DumpBitForSize(uint64_t readable)
{
    if (readable >= 0x800000) return 8;
    if (readable >= 0x400000) return 4;
    if (readable >= 0x100000) return 2;
    if (readable >= 0x40000) return 1;
    return 1;
}

void DumpPixelBufferLayoutOnce(uint64_t pbAddr,
    const CustomJacketPixelBufferInfo& info, const Logger& logger)
{
    const LONG bit = DumpBitForSize(info.readableSize);
    const LONG oldMask = InterlockedOr(&g_dumpMask, bit);
    if (oldMask & bit) return;

    std::ostringstream intro;
    intro << "pb layout dump: base=" << H64(pbAddr)
        << " readable=" << info.readableSize
        << " clone=" << info.cloneSize
        << " tiles=" << info.pageTableWidth << "x" << info.pageTableHeight;
    logger.Log(intro.str());

    for (uint64_t rel = 0; rel < 0x100; rel += 0x20)
    {
        LogQwords(pbAddr, rel, logger);
    }

    const uint64_t samples[] = {
        0, 0x1000, 0x4000, 0x10000, 0x20000,
        0x40000, 0x80000, 0x100000, 0x200000
    };
    for (uint32_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
    {
        LogPage(pbAddr, samples[i], info.readableSize, logger);
    }

    LogMarker(pbAddr, info.readableSize, kDxbc, "DXBC", logger);
    LogMarker(pbAddr, info.readableSize, kDds, "DDS", logger);
    uint64_t hits[4] = {};
    uint32_t count = 0;
    if (SehScanMarker(pbAddr, info.readableSize, kDxbc, hits, count))
    {
        for (uint32_t i = 0; i < count && i < 4; ++i)
        {
            LogDXBCHitContext(pbAddr, hits[i], info.readableSize, logger);
        }
    }
    CustomJacketInternal::DumpPixelBufferPointersOnce(pbAddr, logger);
}
} // namespace CustomJacketInternal
