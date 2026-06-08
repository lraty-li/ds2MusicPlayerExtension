#pragma once

#include "Logger.h"

#include <cstdint>

namespace TextureUploadHistory
{
struct Snapshot
{
    uint64_t callIndex;
    uint64_t textureDx12;
    uint64_t reader;
    uint64_t readerVtable;
    uint64_t callerRva;
    uint64_t header;
    uint64_t slot80;
    uint64_t slot88;
    uint64_t desc90;
    uint64_t slotD0;
    uint64_t slotD8;
    uint64_t descE0;
    uint64_t readerQ8;
    uint64_t readerQ10;
    uint64_t readerQ18;
    uint64_t readerQ20;
    uint64_t readerQ28;
    uint64_t readerQ30;
    uint64_t readerQ38;
    uint64_t readerRegionBase;
    uint64_t readerRegionSize;
    uint64_t stackLow;
    uint64_t stackHigh;
    uint32_t threadId;
    uint32_t readerProtect;
    uint32_t readerType;
    uint32_t readerState;
    uint32_t readerOnStack;
    uint32_t field2C;
    uint32_t field30;
    uint32_t field60;
    uint32_t field64;
};

void Record(const Snapshot& snapshot);
void LogMatches(uint64_t hotPB, uint64_t noDataPB, uint64_t clonePB, const Logger& logger);
} // namespace TextureUploadHistory
