#pragma once

#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

struct ProbeAudioStats
{
    uint32_t availableFrames = 0;
    uint32_t minAvailableFrames = 0;
    uint32_t maxAvailableFrames = 0;
    uint64_t pushedFrames = 0;
    uint64_t consumedFrames = 0;
    uint64_t silenceFrames = 0;
    uint64_t underruns = 0;
    uint64_t trimmedFrames = 0;
    uint64_t overwrittenFrames = 0;
    double rms = 0;
    double peak = 0;
};

class ProbeAudioRing
{
public:
    ProbeAudioRing();

    bool PushPcm16(std::span<const uint8_t> bytes, uint32_t frames);
    bool Ready() const;
    void Consume(uint32_t frames);
    ProbeAudioStats Snapshot(bool resetWindow);

private:
    uint32_t AvailableLocked() const;
    void TrackAvailableLocked(uint32_t available);

    static constexpr uint32_t kChannels = 2;
    static constexpr uint32_t kRingFrames = 48000 * 2;
    static constexpr uint32_t kPrimeFrames = 48000 / 4;
    static constexpr uint32_t kLatencyMaxFrames = 48000 / 2;
    static constexpr uint32_t kLatencyTargetFrames = 48000 / 4;

    mutable std::mutex mutex_;
    std::vector<float> ring_;
    uint64_t readFrame_ = 0;
    uint64_t writeFrame_ = 0;
    bool primed_ = false;
    uint32_t minAvailable_ = kRingFrames;
    uint32_t maxAvailable_ = 0;
    uint64_t pushedFrames_ = 0;
    uint64_t consumedFrames_ = 0;
    uint64_t silenceFrames_ = 0;
    uint64_t underruns_ = 0;
    uint64_t trimmedFrames_ = 0;
    uint64_t overwrittenFrames_ = 0;
    long double squareSum_ = 0;
    uint64_t sampleCount_ = 0;
    double peak_ = 0;
};
