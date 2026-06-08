#include "pch.h"

#include "CustomJacketInternal.h"

#include "TextureUploadHistory.h"

#include <sstream>

namespace
{
LONG g_logged = 0;

struct VqInfo
{
    bool ok = false;
    uint64_t base = 0;
    uint64_t size = 0;
    DWORD protect = 0;
    DWORD type = 0;
    DWORD state = 0;
};

struct ResourceLink
{
    uint64_t wrapper = 0;
    uint64_t resource = 0;
    uint64_t cpuData = 0;
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

const char* RegionKind(uint64_t ptr)
{
    const VqInfo vq = Query(ptr);
    if (!ptr) return "null";
    if (!vq.ok || vq.state != MEM_COMMIT || !IsReadableProtect(vq.protect))
    {
        return "unreadable";
    }
    if (vq.type == MEM_IMAGE) return "image";
    return "heap";
}

void LogQwords(const char* label, uint64_t base, uint64_t bytes, const Logger& logger)
{
    for (uint64_t rel = 0; rel < bytes; rel += 0x20)
    {
        uint64_t q[4] = {};
        for (uint32_t i = 0; i < 4; ++i)
        {
            Read64(base + rel + i * 8, q[i]);
        }
        std::ostringstream oss;
        oss << "pbres q " << label << "+" << H(rel) << ": "
            << H(q[0]) << " " << H(q[1]) << " "
            << H(q[2]) << " " << H(q[3]);
        logger.Log(oss.str());
    }
}

void LogPtr(const char* label, const char* field, uint64_t ptr, const Logger& logger)
{
    const VqInfo vq = Query(ptr);
    std::ostringstream oss;
    oss << "pbres ptr " << label
        << " " << field << "=" << H(ptr)
        << " kind=" << RegionKind(ptr);
    if (vq.ok)
    {
        oss << " base=" << H(vq.base)
            << " region=" << vq.size
            << " protect=" << H(vq.protect)
            << " type=" << H(vq.type)
            << " state=" << H(vq.state);
    }
    logger.Log(oss.str());
}

void LogFieldPtr(const char* label, uint64_t owner,
    uint64_t field, const char* name, const Logger& logger)
{
    uint64_t ptr = 0;
    if (!Read64(owner + field, ptr))
    {
        std::ostringstream oss;
        oss << "pbres ptr " << label << " " << name << " read=fail";
        logger.Log(oss.str());
        return;
    }
    LogPtr(label, name, ptr, logger);
}

void LogDescriptorBlock(const char* label,
    const char* name, uint64_t ptr, const Logger& logger)
{
    if (!ptr) return;
    std::ostringstream block;
    block << label << "." << name;
    LogQwords(block.str().c_str(), ptr, 0x80, logger);
}

ResourceLink ReadResourceLink(uint64_t pb)
{
    ResourceLink out = {};
    if (!Read64(pb + 0x88, out.wrapper) || !out.wrapper) return out;
    Read64(out.wrapper + 0x08, out.resource);
    Read64(out.wrapper + 0x30, out.cpuData);
    return out;
}

void LogResourceObject(const char* label,
    const char* name, uint64_t ptr, const Logger& logger)
{
    if (!ptr) return;

    uint64_t vt = 0;
    uint64_t slot40 = 0;
    uint64_t slot50 = 0;
    uint64_t slot58 = 0;
    Read64(ptr, vt);
    if (vt)
    {
        Read64(vt + 0x40, slot40);
        Read64(vt + 0x50, slot50);
        Read64(vt + 0x58, slot58);
    }

    std::ostringstream oss;
    oss << "pbres resource " << label
        << " " << name << "+0x8=" << H(ptr)
        << " vt=" << H(vt)
        << " vt40=" << H(slot40)
        << " vt50=" << H(slot50)
        << " vt58=" << H(slot58);
    logger.Log(oss.str());

    std::ostringstream block;
    block << name << ".res";
    LogQwords(block.str().c_str(), ptr, 0x80, logger);
}

void LogResourceWrapper(const char* label,
    const char* name, uint64_t ptr, const Logger& logger)
{
    if (!ptr) return;
    std::ostringstream intro;
    intro << "pbres wrapper " << label
        << " " << name << "=" << H(ptr);
    logger.Log(intro.str());
    LogQwords(name, ptr, 0xA0, logger);

    const uint64_t fields[] = { 0x08, 0x20, 0x30, 0x40, 0x88, 0x90 };
    for (uint32_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
    {
        uint64_t value = 0;
        if (!Read64(ptr + fields[i], value)) continue;
        std::ostringstream fname;
        fname << name << "+" << H(fields[i]);
        LogPtr(label, fname.str().c_str(), value, logger);
    }

    uint64_t resource = 0;
    if (Read64(ptr + 0x08, resource))
    {
        LogResourceObject(label, name, resource, logger);
    }
}

void LogPixelBuffer(const char* label, uint64_t pb, const Logger& logger)
{
    if (!pb) return;
    std::ostringstream intro;
    intro << "pbres begin " << label << "=" << H(pb);
    logger.Log(intro.str());
    LogQwords(label, pb, 0x100, logger);

    uint64_t resource80 = 0;
    uint64_t resource88 = 0;
    uint64_t desc90 = 0;
    uint64_t descE0 = 0;
    Read64(pb + 0x80, resource80);
    Read64(pb + 0x88, resource88);
    Read64(pb + 0x90, desc90);
    Read64(pb + 0xE0, descE0);

    LogFieldPtr(label, pb, 0x80, "f80", logger);
    LogFieldPtr(label, pb, 0x88, "f88", logger);
    LogFieldPtr(label, pb, 0x90, "f90", logger);
    LogFieldPtr(label, pb, 0xB8, "fB8", logger);
    LogFieldPtr(label, pb, 0xE0, "fE0", logger);
    LogResourceWrapper(label, "f80", resource80, logger);
    LogResourceWrapper(label, "f88", resource88, logger);
    LogDescriptorBlock(label, "f90", desc90, logger);
    LogDescriptorBlock(label, "fE0", descE0, logger);
}

void LogLinkComparison(uint64_t hotPB, uint64_t noDataPB,
    uint64_t clonePB, const Logger& logger)
{
    const ResourceLink hot = ReadResourceLink(hotPB);
    const ResourceLink noData = ReadResourceLink(noDataPB);
    const ResourceLink clone = ReadResourceLink(clonePB);

    std::ostringstream oss;
    oss << "pbres link f88 hot.wrapper=" << H(hot.wrapper)
        << " hot.resource=" << H(hot.resource)
        << " hot.cpu=" << H(hot.cpuData)
        << " noData.wrapper=" << H(noData.wrapper)
        << " noData.resource=" << H(noData.resource)
        << " noData.cpu=" << H(noData.cpuData)
        << " clone.wrapper=" << H(clone.wrapper)
        << " clone.resource=" << H(clone.resource)
        << " clone.cpu=" << H(clone.cpuData)
        << " cloneResEqHot=" << (clone.resource == hot.resource ? 1 : 0)
        << " cloneCpuEqHot=" << (clone.cpuData == hot.cpuData ? 1 : 0);
    logger.Log(oss.str());
}

} // namespace

namespace CustomJacketInternal
{
void ResetPixelBufferGpuResourceDiagnostics()
{
    InterlockedExchange(&g_logged, 0);
}

void DumpPixelBufferGpuResourceOnce(uint64_t hotPB, uint64_t noDataPB,
    uint64_t clonePB, const Logger& logger)
{
    if (!hotPB || !noDataPB || !clonePB) return;
    if (InterlockedExchange(&g_logged, 1) != 0) return;

    logger.Log("pbres begin TextureDX12 GPU resource comparison");
    LogLinkComparison(hotPB, noDataPB, clonePB, logger);
    DumpPixelBufferHandleSlots(hotPB, noDataPB, clonePB, logger);
    TextureUploadHistory::LogMatches(hotPB, noDataPB, clonePB, logger);
    LogPixelBuffer("hot", hotPB, logger);
    LogPixelBuffer("noData", noDataPB, logger);
    LogPixelBuffer("clone", clonePB, logger);
}
} // namespace CustomJacketInternal
