#pragma once

#include <cstdint>

namespace AudioRingBuffer
{
struct Stats
{
    uint32_t availableFrames = 0;
    uint32_t minAvailableFrames = 0;
    uint32_t maxAvailableFrames = 0;
    uint64_t pushFrames = 0;
    uint64_t readCalls = 0;
    uint64_t readFramesRequested = 0;
    uint64_t readFramesCopied = 0;
    uint64_t silenceFrames = 0;
    uint64_t underruns = 0;
    uint64_t lockMisses = 0;
    uint64_t shortReads = 0;
    uint64_t trimmedFrames = 0;
    uint64_t overwrittenFrames = 0;
};

void PushPcm16(const uint8_t* pcm, uint32_t frames, uint16_t channels);
void PushFloat32(const uint8_t* samples, uint32_t frames, uint16_t channels);
void Clear();
uint32_t Read(float* const* outputs, uint32_t frames, uint32_t channels);
uint32_t AvailableFrames();
uint64_t Underruns();
Stats SnapshotStats(bool resetWindow);
}
