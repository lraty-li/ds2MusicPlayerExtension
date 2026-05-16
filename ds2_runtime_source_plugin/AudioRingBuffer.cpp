#include "pch.h"

#include "AudioRingBuffer.h"

#include <cstdint>
#include <atomic>

namespace
{
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kRingFrames = kSampleRate * 2;
constexpr uint32_t kRingChannels = 2;
constexpr uint32_t kLatencyMaxFrames = kSampleRate / 10;
constexpr uint32_t kLatencyTargetFrames = kSampleRate / 20;
constexpr float kPcm16Scale = 1.0f / 32768.0f;

SRWLOCK g_lock = SRWLOCK_INIT;
float g_ring[kRingFrames * kRingChannels] = {};
uint32_t g_readFrame = 0;
uint32_t g_writeFrame = 0;
uint32_t g_availableFrames = 0;
std::atomic<uint64_t> g_underruns{0};
}

namespace AudioRingBuffer
{
void PushPcm16(const uint8_t* pcm, uint32_t frames, uint16_t channels)
{
    if (!pcm || frames == 0 || channels == 0) return;
    AcquireSRWLockExclusive(&g_lock);
    const auto* samples = reinterpret_cast<const int16_t*>(pcm);
    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        const uint32_t src = frame * channels;
        const float left = static_cast<float>(samples[src]) * kPcm16Scale;
        const float right = channels > 1 ?
            static_cast<float>(samples[src + 1]) * kPcm16Scale :
            left;
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
    ReleaseSRWLockExclusive(&g_lock);
}

uint32_t Read(float* const* outputs, uint32_t frames, uint32_t channels)
{
    if (!outputs || frames == 0 || channels == 0) return 0;
    if (!TryAcquireSRWLockExclusive(&g_lock))
    {
        g_underruns.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    uint32_t copied = 0;
    while (copied < frames && g_availableFrames > 0)
    {
        const uint32_t src = g_readFrame * kRingChannels;
        for (uint32_t ch = 0; ch < channels; ++ch)
        {
            if (outputs[ch])
            {
                outputs[ch][copied] = g_ring[src + (ch > 0 ? 1 : 0)];
            }
        }
        g_readFrame = (g_readFrame + 1) % kRingFrames;
        --g_availableFrames;
        ++copied;
    }
    if (copied < frames)
    {
        g_underruns.fetch_add(1, std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return copied;
}

uint32_t AvailableFrames()
{
    AcquireSRWLockShared(&g_lock);
    const uint32_t frames = g_availableFrames;
    ReleaseSRWLockShared(&g_lock);
    return frames;
}

uint64_t Underruns()
{
    return g_underruns.load(std::memory_order_relaxed);
}
}
