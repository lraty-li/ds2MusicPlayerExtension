#include "pch.h"

#include "AudioRingBuffer.h"

#include <cstdint>
#include <mutex>

namespace
{
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kRingFrames = kSampleRate * 2;
constexpr uint32_t kRingChannels = 2;
constexpr uint32_t kLatencyMaxFrames = kSampleRate / 10;
constexpr uint32_t kLatencyTargetFrames = kSampleRate / 20;

std::mutex g_mutex;
float g_ring[kRingFrames * kRingChannels] = {};
uint32_t g_readFrame = 0;
uint32_t g_writeFrame = 0;
uint32_t g_availableFrames = 0;
uint64_t g_underruns = 0;
}

namespace AudioRingBuffer
{
void PushPcm16(const uint8_t* pcm, uint32_t frames, uint16_t channels)
{
    if (!pcm || channels == 0) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto* samples = reinterpret_cast<const int16_t*>(pcm);
    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        const uint32_t src = frame * channels;
        const float left = samples[src] / 32768.0f;
        const float right = channels > 1 ? samples[src + 1] / 32768.0f : left;
        const uint32_t dst = g_writeFrame * kRingChannels;
        g_ring[dst] = left;
        g_ring[dst + 1] = right;
        g_writeFrame = (g_writeFrame + 1) % kRingFrames;
        if (g_availableFrames < kRingFrames) ++g_availableFrames;
        else g_readFrame = (g_readFrame + 1) % kRingFrames;
    }

    if (g_availableFrames > kLatencyMaxFrames)
    {
        const uint32_t drop = g_availableFrames - kLatencyTargetFrames;
        g_readFrame = (g_readFrame + drop) % kRingFrames;
        g_availableFrames -= drop;
    }
}

uint32_t Read(float* output, uint32_t frames, uint32_t channels)
{
    if (!output || channels == 0) return 0;
    uint32_t copied = 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    while (copied < frames && g_availableFrames > 0)
    {
        const uint32_t src = g_readFrame * kRingChannels;
        for (uint32_t ch = 0; ch < channels; ++ch)
        {
            output[ch * frames + copied] = g_ring[src + (ch > 0 ? 1 : 0)];
        }
        g_readFrame = (g_readFrame + 1) % kRingFrames;
        --g_availableFrames;
        ++copied;
    }
    if (copied < frames) ++g_underruns;
    return copied;
}

uint32_t AvailableFrames()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_availableFrames;
}

uint64_t Underruns()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_underruns;
}
}
