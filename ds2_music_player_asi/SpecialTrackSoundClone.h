#pragma once

#include <cstdint>

struct SpecialTrackCloneChainResult
{
    void* gsr = nullptr;
    void* gpr = nullptr;
    void* ncr = nullptr;
    void* wwiseId = nullptr;
    void** dsloEntries = nullptr;
    uint32_t oldEventId = 0;
};

namespace SpecialTrackSoundClone
{
uint32_t ReadEventIdFromGsr(void* gsr);
bool Build(void* sourceGsr, SpecialTrackCloneChainResult& result);
} // namespace SpecialTrackSoundClone
