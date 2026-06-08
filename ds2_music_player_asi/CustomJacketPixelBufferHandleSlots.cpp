#include "pch.h"

#include "CustomJacketInternal.h"

#include <sstream>

namespace
{
struct HandleSlot
{
    uint64_t q0 = 0;
    uint64_t q8 = 0;
    uint64_t q10 = 0;
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

HandleSlot ReadHandleSlot(uint64_t textureDx12, uint64_t offset)
{
    HandleSlot out = {};
    Read64(textureDx12 + offset, out.q0);
    Read64(textureDx12 + offset + 0x08, out.q8);
    Read64(textureDx12 + offset + 0x10, out.q10);
    return out;
}

void LogHandleSlotLine(const char* name, uint64_t offset,
    uint64_t hotPB, uint64_t noDataPB, uint64_t clonePB, const Logger& logger)
{
    const HandleSlot hot = ReadHandleSlot(hotPB, offset);
    const HandleSlot noData = ReadHandleSlot(noDataPB, offset);
    const HandleSlot clone = ReadHandleSlot(clonePB, offset);

    std::ostringstream oss;
    oss << "pbres handle " << name
        << " hot=[" << H(hot.q0) << "," << H(hot.q8) << "," << H(hot.q10) << "]"
        << " noData=[" << H(noData.q0) << "," << H(noData.q8) << "," << H(noData.q10) << "]"
        << " clone=[" << H(clone.q0) << "," << H(clone.q8) << "," << H(clone.q10) << "]"
        << " cloneQ0EqHot=" << (clone.q0 == hot.q0 ? 1 : 0)
        << " cloneQ8EqHot=" << (clone.q8 == hot.q8 ? 1 : 0)
        << " cloneQ10EqHot=" << (clone.q10 == hot.q10 ? 1 : 0)
        << " cloneQ8EqNoData=" << (clone.q8 == noData.q8 ? 1 : 0)
        << " cloneQ10EqNoData=" << (clone.q10 == noData.q10 ? 1 : 0);
    logger.Log(oss.str());
}
} // namespace

namespace CustomJacketInternal
{
void DumpPixelBufferHandleSlots(uint64_t hotPB, uint64_t noDataPB,
    uint64_t clonePB, const Logger& logger)
{
    LogHandleSlotLine("slot78", 0x78, hotPB, noDataPB, clonePB, logger);
    LogHandleSlotLine("slotC8", 0xC8, hotPB, noDataPB, clonePB, logger);
}
} // namespace CustomJacketInternal
