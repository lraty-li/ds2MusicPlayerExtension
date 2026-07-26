#include "ProbeAudioRing.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

ProbeAudioRing::ProbeAudioRing()
    : ring_(kRingFrames * kChannels)
{
}

bool ProbeAudioRing::PushPcm16(
    std::span<const uint8_t> bytes,
    uint32_t frames)
{
    const uint64_t expectedBytes =
        static_cast<uint64_t>(frames) * kChannels * sizeof(int16_t);
    if (frames == 0 || bytes.size() != expectedBytes)
    {
        return false;
    }
    std::lock_guard lock(mutex_);
    const uint64_t endWrite = writeFrame_ + frames;
    if (endWrite > readFrame_ + kRingFrames)
    {
        const uint64_t newRead = endWrite - kRingFrames;
        overwrittenFrames_ += newRead - readFrame_;
        readFrame_ = newRead;
    }
    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        const size_t source = frame * kChannels * sizeof(int16_t);
        int16_t left = 0;
        int16_t right = 0;
        std::memcpy(&left, bytes.data() + source, sizeof(left));
        std::memcpy(
            &right,
            bytes.data() + source + sizeof(left),
            sizeof(right));
        const size_t target =
            static_cast<size_t>((writeFrame_ + frame) % kRingFrames) *
            kChannels;
        ring_[target] = static_cast<float>(left) / 32768.0f;
        ring_[target + 1] = static_cast<float>(right) / 32768.0f;
    }
    writeFrame_ = endWrite;
    pushedFrames_ += frames;
    uint32_t available = AvailableLocked();
    if (available > kLatencyMaxFrames)
    {
        const uint64_t newRead = writeFrame_ - kLatencyTargetFrames;
        trimmedFrames_ += newRead - readFrame_;
        readFrame_ = newRead;
        available = AvailableLocked();
    }
    TrackAvailableLocked(available);
    return true;
}

bool ProbeAudioRing::Ready() const
{
    std::lock_guard lock(mutex_);
    return AvailableLocked() >= kPrimeFrames;
}

void ProbeAudioRing::Consume(uint32_t frames)
{
    std::lock_guard lock(mutex_);
    const uint32_t available = AvailableLocked();
    const uint32_t copied = std::min(frames, available);
    for (uint32_t frame = 0; frame < copied; ++frame)
    {
        const size_t source =
            static_cast<size_t>((readFrame_ + frame) % kRingFrames) *
            kChannels;
        for (uint32_t channel = 0; channel < kChannels; ++channel)
        {
            const double sample = ring_[source + channel];
            squareSum_ += sample * sample;
            peak_ = std::max(peak_, std::abs(sample));
            ++sampleCount_;
        }
    }
    readFrame_ += copied;
    consumedFrames_ += copied;
    if (copied < frames)
    {
        silenceFrames_ += frames - copied;
        ++underruns_;
    }
    TrackAvailableLocked(AvailableLocked());
}

ProbeAudioStats ProbeAudioRing::Snapshot(bool resetWindow)
{
    std::lock_guard lock(mutex_);
    ProbeAudioStats stats{};
    stats.availableFrames = AvailableLocked();
    stats.minAvailableFrames =
        minAvailable_ == kRingFrames ? stats.availableFrames : minAvailable_;
    stats.maxAvailableFrames = maxAvailable_;
    stats.pushedFrames = pushedFrames_;
    stats.consumedFrames = consumedFrames_;
    stats.silenceFrames = silenceFrames_;
    stats.underruns = underruns_;
    stats.trimmedFrames = trimmedFrames_;
    stats.overwrittenFrames = overwrittenFrames_;
    stats.rms = sampleCount_ > 0
        ? std::sqrt(static_cast<double>(squareSum_ / sampleCount_))
        : 0;
    stats.peak = peak_;
    if (resetWindow)
    {
        minAvailable_ = stats.availableFrames;
        maxAvailable_ = stats.availableFrames;
        squareSum_ = 0;
        sampleCount_ = 0;
        peak_ = 0;
    }
    return stats;
}

uint32_t ProbeAudioRing::AvailableLocked() const
{
    if (writeFrame_ <= readFrame_) return 0;
    const uint64_t available = writeFrame_ - readFrame_;
    return available > kRingFrames
        ? kRingFrames
        : static_cast<uint32_t>(available);
}

void ProbeAudioRing::TrackAvailableLocked(uint32_t available)
{
    minAvailable_ = std::min(minAvailable_, available);
    maxAvailable_ = std::max(maxAvailable_, available);
}
