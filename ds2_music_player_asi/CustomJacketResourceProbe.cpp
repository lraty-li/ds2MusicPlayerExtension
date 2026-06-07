#include "pch.h"

#include "CustomJacketInternal.h"

#include <sstream>

namespace
{
LONG g_dumped = 0;
LONG g_catalogueDumped = 0;

struct ArrayHeader
{
    uint32_t count;
    uint32_t capacity;
    uint64_t data;
};

std::string H64(uint64_t value)
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

std::string H32(uint32_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << value;
    return oss.str();
}

bool ReadArray(uint64_t base, uint32_t offset, ArrayHeader& out)
{
    __try
    {
        out.count = *reinterpret_cast<uint32_t*>(base + offset);
        out.capacity = *reinterpret_cast<uint32_t*>(base + offset + 4);
        out.data = *reinterpret_cast<uint64_t*>(base + offset + 8);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out = {};
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

void LogDwords(const char* label, uint64_t base, uint32_t offset, const Logger& logger)
{
    uint32_t d[4] = {};
    for (uint32_t i = 0; i < 4; ++i)
    {
        Read32(base + offset + i * 4, d[i]);
    }

    std::ostringstream oss;
    oss << "jres " << label << " d+0x" << std::hex << offset << ": "
        << "0x" << d[0] << " 0x" << d[1] << " 0x" << d[2] << " 0x" << d[3];
    logger.Log(oss.str());
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

void LogStreamingRefArray(const char* label, uint64_t base,
    uint32_t offset, const Logger& logger)
{
    ArrayHeader array = {};
    if (!ReadArray(base, offset, array))
    {
        logger.Log(std::string("jres array failed: ") + label);
        return;
    }

    std::ostringstream intro;
    intro << "jres array " << label
        << " off=" << offset
        << " count=" << array.count
        << " capacity=" << array.capacity
        << " data=" << H64(array.data);
    logger.Log(intro.str());

    const uint32_t count = array.count < 6 ? array.count : 6;
    for (uint32_t i = 0; i < count; ++i)
    {
        uint64_t slotPtr = 0;
        if (!CustomJacketInternal::SehReadU64(array.data + i * 8, slotPtr))
        {
            continue;
        }
        std::ostringstream name;
        name << label << "[" << i << "]";
        LogSlotPointer(name.str().c_str(), slotPtr, logger);
    }
}

void LogU32Array(const char* label, uint64_t base, uint32_t offset, const Logger& logger)
{
    ArrayHeader array = {};
    if (!ReadArray(base, offset, array))
    {
        logger.Log(std::string("jres u32 array failed: ") + label);
        return;
    }

    std::ostringstream oss;
    oss << "jres u32array " << label
        << " off=" << offset
        << " count=" << array.count
        << " capacity=" << array.capacity
        << " data=" << H64(array.data)
        << " values=";

    const uint32_t count = array.count < 10 ? array.count : 10;
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t value = 0;
        if (i) oss << ",";
        if (Read32(array.data + i * 4, value))
        {
            oss << H32(value);
        }
        else
        {
            oss << "read_failed";
        }
    }
    logger.Log(oss.str());
}

void LogCandidate(const char* label, uint64_t ptr, const Logger& logger)
{
    if (!ptr)
    {
        return;
    }

    uint64_t q0 = 0;
    uint64_t q1 = 0;
    if (!CustomJacketInternal::SehReadU64(ptr, q0)
        || !CustomJacketInternal::SehReadU64(ptr + 8, q1))
    {
        return;
    }

    std::ostringstream oss;
    oss << "jres candidate " << label << "=" << H64(ptr)
        << " q0=" << H64(q0)
        << " q1=" << H64(q1);
    logger.Log(oss.str());
}
} // namespace

namespace CustomJacketInternal
{
void ResetResourceProbeDiagnostics()
{
    InterlockedExchange(&g_dumped, 0);
    InterlockedExchange(&g_catalogueDumped, 0);
    ResetTrackAlbumProbeDiagnostics();
}

void DumpCatalogueResourceProbeOnce(void* catalogueResource, const Logger& logger)
{
    if (InterlockedExchange(&g_catalogueDumped, 1))
    {
        return;
    }

    const uint64_t base = reinterpret_cast<uint64_t>(catalogueResource);
    std::ostringstream intro;
    intro << "jres catalogue=" << H64(base)
        << " fields=MusicJacketImageTextures/DefaultMusicJacketImageTexture";
    logger.Log(intro.str());

    LogQwords("catalogue", base, 0xE0, logger);
    LogStreamingRefArray("MissionImageTextures", base, 48, logger);
    LogStreamingRefArray("HotSpringImageTextures", base, 80, logger);
    LogStreamingRefArray("MusicJacketImageTextures", base, 96, logger);
    LogStreamingRefArray("ConstructionHoloImageTextures", base, 112, logger);
    LogStreamingRefArray("CostumeCustomizeImageTextures", base, 128, logger);

    uint64_t defaultMusicSlot = 0;
    CustomJacketInternal::SehReadU64(base + 192, defaultMusicSlot);
    LogSlotPointer("DefaultMusicJacketImageTexture", defaultMusicSlot, logger);

    LogU32Array("MusicJacketImageNameHash", base, 280, logger);

    uint32_t defaultHash = 0;
    if (Read32(base + 376, defaultHash))
    {
        std::ostringstream oss;
        oss << "jres DefaultMusicJacketImageNameHash=" << H32(defaultHash);
        logger.Log(oss.str());
    }
}

void DumpResourceJacketProbeOnce(uint64_t slotAddr, const CustomJacketSlot& slot,
    uint64_t loaded, uint64_t texture, const Logger& logger)
{
    if (InterlockedExchange(&g_dumped, 1))
    {
        return;
    }

    std::ostringstream intro;
    intro << "jres slot=" << H64(slotAddr)
        << " target=" << H64(slot.target)
        << " packed=" << H64(slot.packed)
        << " loadedUI=" << H64(loaded)
        << " texture=" << H64(texture);
    logger.Log(intro.str());

    LogQwords("target", slot.target, 0x30, logger);
    LogQwords("ui", loaded, 0x80, logger);
    LogQwords("tex", texture, 0x100, logger);
    LogDwords("ui", loaded, 0x20, logger);
    LogDwords("tex", texture, 0x20, logger);

    uint64_t targetResource = 0;
    uint64_t uiTexture = 0;
    uint64_t texturePixelBuffer = 0;
    uint64_t textureMaybeData = 0;
    SehReadU64(slot.target, targetResource);
    SehReadU64(loaded + 0x30, uiTexture);
    SehReadU64(texture + 0x20, texturePixelBuffer);
    SehReadU64(texture + 0x30, textureMaybeData);

    LogCandidate("target.q0", targetResource, logger);
    LogCandidate("ui+0x30", uiTexture, logger);
    LogCandidate("tex+0x20", texturePixelBuffer, logger);
    LogCandidate("tex+0x30", textureMaybeData, logger);
}
} // namespace CustomJacketInternal
