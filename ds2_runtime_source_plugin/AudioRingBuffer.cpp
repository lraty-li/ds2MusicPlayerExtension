#include "pch.h"

#include "AudioRingBuffer.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace
{
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kRingFrames = kSampleRate * 2;
constexpr uint32_t kRingChannels = 2;
constexpr uint32_t kLatencyMaxFrames = kSampleRate / 2;
constexpr uint32_t kLatencyTargetFrames = kSampleRate / 4;
constexpr float kPcm16Scale = 1.0f / 32768.0f;

float g_ring[kRingFrames * kRingChannels] = {};
std::atomic<uint64_t> g_readFrame{0};
std::atomic<uint64_t> g_writeFrame{0};
std::atomic<bool> g_primed{false};
std::atomic<uint32_t> g_minAvailableFrames{kRingFrames};
std::atomic<uint32_t> g_maxAvailableFrames{0};
std::atomic<uint64_t> g_pushFrames{0};
std::atomic<uint64_t> g_readCalls{0};
std::atomic<uint64_t> g_readFramesRequested{0};
std::atomic<uint64_t> g_readFramesCopied{0};
std::atomic<uint64_t> g_silenceFrames{0};
std::atomic<uint64_t> g_underruns{0};
std::atomic<uint64_t> g_shortReads{0};
std::atomic<uint64_t> g_trimmedFrames{0};
std::atomic<uint64_t> g_overwrittenFrames{0};

uint32_t ClampAvailable(uint64_t read, uint64_t write)
{
    if (write <= read) return 0;
    const uint64_t available = write - read;
    return available > kRingFrames ? kRingFrames :
        static_cast<uint32_t>(available);
}

uint64_t AdvanceReadAtLeast(uint64_t target)
{
    uint64_t current = g_readFrame.load(std::memory_order_acquire);
    while (current < target)
    {
        if (g_readFrame.compare_exchange_weak(current, target,
            std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return target - current;
        }
    }
    return 0;
}

void TrackAvailable(uint32_t available)
{
    uint32_t minValue = g_minAvailableFrames.load(std::memory_order_relaxed);
    while (available < minValue &&
        !g_minAvailableFrames.compare_exchange_weak(minValue, available,
            std::memory_order_relaxed, std::memory_order_relaxed))
    {
    }

    uint32_t maxValue = g_maxAvailableFrames.load(std::memory_order_relaxed);
    while (available > maxValue &&
        !g_maxAvailableFrames.compare_exchange_weak(maxValue, available,
            std::memory_order_relaxed, std::memory_order_relaxed))
    {
    }
}

void WriteFrame(uint64_t frame, float left, float right)
{
    const uint32_t dst =
        static_cast<uint32_t>(frame % kRingFrames) * kRingChannels;
    g_ring[dst] = left;
    g_ring[dst + 1] = right;
}

float ReadFloat32(const uint8_t* data)
{
    float value = 0.0f;
    memcpy(&value, data, sizeof(value));
    return value;
}

void ReserveFrames(uint64_t endWrite)
{
    if (endWrite > kRingFrames)
    {
        const uint64_t minRead = endWrite - kRingFrames;
        const uint64_t dropped = AdvanceReadAtLeast(minRead);
        g_overwrittenFrames.fetch_add(dropped, std::memory_order_relaxed);
    }
}

void PublishFrames(uint64_t endWrite)
{
    g_writeFrame.store(endWrite, std::memory_order_release);

    const uint64_t read = g_readFrame.load(std::memory_order_acquire);
    uint32_t available = ClampAvailable(read, endWrite);
    if (available > kLatencyMaxFrames)
    {
        const uint64_t dropped =
            AdvanceReadAtLeast(endWrite - kLatencyTargetFrames);
        g_trimmedFrames.fetch_add(dropped, std::memory_order_relaxed);
        const uint64_t trimmedRead = g_readFrame.load(std::memory_order_acquire);
        available = ClampAvailable(trimmedRead, endWrite);
    }
    TrackAvailable(available);
}
}

namespace AudioRingBuffer
{
void PushPcm16(const uint8_t* pcm, uint32_t frames, uint16_t channels)
{
    if (!pcm || frames == 0 || channels == 0) return;

    const auto* samples = reinterpret_cast<const int16_t*>(pcm);
    const uint64_t startWrite = g_writeFrame.load(std::memory_order_relaxed);
    const uint64_t endWrite = startWrite + frames;
    ReserveFrames(endWrite);
    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        const uint32_t src = frame * channels;
        const float left = static_cast<float>(samples[src]) * kPcm16Scale;
        const float right = channels > 1 ?
            static_cast<float>(samples[src + 1]) * kPcm16Scale :
            left;
        WriteFrame(startWrite + frame, left, right);
    }
    g_pushFrames.fetch_add(frames, std::memory_order_relaxed);
    PublishFrames(endWrite);
}

void PushFloat32(const uint8_t* samples, uint32_t frames, uint16_t channels)
{
    if (!samples || frames == 0 || channels == 0) return;

    const uint64_t startWrite = g_writeFrame.load(std::memory_order_relaxed);
    const uint64_t endWrite = startWrite + frames;
    ReserveFrames(endWrite);
    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        const uint32_t src = frame * channels * sizeof(float);
        const float left = ReadFloat32(samples + src);
        const float right = channels > 1 ?
            ReadFloat32(samples + src + sizeof(float)) :
            left;
        WriteFrame(startWrite + frame, left, right);
    }
    g_pushFrames.fetch_add(frames, std::memory_order_relaxed);
    PublishFrames(endWrite);
}

uint32_t Read(float* const* outputs, uint32_t frames, uint32_t channels)
{
    if (!outputs || frames == 0 || channels == 0) return 0;

    g_readCalls.fetch_add(1, std::memory_order_relaxed);
    g_readFramesRequested.fetch_add(frames, std::memory_order_relaxed);

    uint64_t read = g_readFrame.load(std::memory_order_acquire);
    const uint64_t write = g_writeFrame.load(std::memory_order_acquire);
    if (write > read + kRingFrames)
    {
        read = write - kRingFrames;
        AdvanceReadAtLeast(read);
    }

    const uint32_t available = ClampAvailable(read, write);
    if (!g_primed.load(std::memory_order_relaxed))
    {
        if (available < kLatencyTargetFrames)
        {
            g_shortReads.fetch_add(1, std::memory_order_relaxed);
            g_silenceFrames.fetch_add(frames, std::memory_order_relaxed);
            g_underruns.fetch_add(1, std::memory_order_relaxed);
            TrackAvailable(available);
            return 0;
        }
        g_primed.store(true, std::memory_order_relaxed);
    }
    const uint32_t copied = frames < available ? frames : available;
    for (uint32_t frame = 0; frame < copied; ++frame)
    {
        const uint32_t src = static_cast<uint32_t>(
            (read + frame) % kRingFrames) * kRingChannels;
        for (uint32_t ch = 0; ch < channels; ++ch)
        {
            if (outputs[ch])
            {
                outputs[ch][frame] = g_ring[src + (ch > 0 ? 1 : 0)];
            }
        }
    }

    AdvanceReadAtLeast(read + copied);
    g_readFramesCopied.fetch_add(copied, std::memory_order_relaxed);
    if (copied < frames)
    {
        g_primed.store(false, std::memory_order_relaxed);
        g_shortReads.fetch_add(1, std::memory_order_relaxed);
        g_silenceFrames.fetch_add(frames - copied, std::memory_order_relaxed);
        g_underruns.fetch_add(1, std::memory_order_relaxed);
    }

    const uint64_t currentRead = g_readFrame.load(std::memory_order_acquire);
    TrackAvailable(ClampAvailable(currentRead, write));
    return copied;
}

uint32_t AvailableFrames()
{
    const uint64_t read = g_readFrame.load(std::memory_order_acquire);
    const uint64_t write = g_writeFrame.load(std::memory_order_acquire);
    return ClampAvailable(read, write);
}

uint64_t Underruns()
{
    return g_underruns.load(std::memory_order_relaxed);
}

Stats SnapshotStats(bool resetWindow)
{
    const uint32_t available = AvailableFrames();
    Stats stats = {};
    stats.availableFrames = available;
    stats.minAvailableFrames =
        g_minAvailableFrames.load(std::memory_order_relaxed);
    if (stats.minAvailableFrames == kRingFrames)
    {
        stats.minAvailableFrames = available;
    }
    stats.maxAvailableFrames =
        g_maxAvailableFrames.load(std::memory_order_relaxed);
    stats.pushFrames = g_pushFrames.load(std::memory_order_relaxed);
    stats.readCalls = g_readCalls.load(std::memory_order_relaxed);
    stats.readFramesRequested =
        g_readFramesRequested.load(std::memory_order_relaxed);
    stats.readFramesCopied = g_readFramesCopied.load(std::memory_order_relaxed);
    stats.silenceFrames = g_silenceFrames.load(std::memory_order_relaxed);
    stats.underruns = g_underruns.load(std::memory_order_relaxed);
    stats.lockMisses = 0;
    stats.shortReads = g_shortReads.load(std::memory_order_relaxed);
    stats.trimmedFrames = g_trimmedFrames.load(std::memory_order_relaxed);
    stats.overwrittenFrames =
        g_overwrittenFrames.load(std::memory_order_relaxed);

    if (resetWindow)
    {
        g_minAvailableFrames.store(available, std::memory_order_relaxed);
        g_maxAvailableFrames.store(available, std::memory_order_relaxed);
    }
    return stats;
}
}
