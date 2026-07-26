#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <string_view>

class PcmStreamReceiver
{
public:
    bool HandleMessage(
        std::wstring_view message,
        std::wstring& metricsJson);

private:
    void ResetStream(
        std::wstring_view streamId,
        uint32_t sampleRate,
        uint32_t channels,
        ULONGLONG now);
    std::wstring BuildMetricsJson(
        ULONGLONG now,
        bool warmingUp);
    void ResetInterval(ULONGLONG now);

    std::wstring streamId_;
    std::wstring lastError_;
    uint32_t sampleRate_ = 0;
    uint32_t channels_ = 0;
    uint64_t expectedSequence_ = 0;
    uint64_t lastSequence_ = 0;
    uint64_t totalChunks_ = 0;
    uint64_t totalFrames_ = 0;
    uint64_t totalBytes_ = 0;
    uint64_t sequenceGaps_ = 0;
    uint64_t outOfOrder_ = 0;
    uint64_t invalidChunks_ = 0;
    uint64_t checksum_ = 14695981039346656037ull;
    ULONGLONG intervalStarted_ = 0;
    uint64_t intervalFrames_ = 0;
    uint64_t intervalBytes_ = 0;
    uint64_t intervalSamples_ = 0;
    uint64_t intervalNonzero_ = 0;
    long double intervalSumSquares_ = 0;
    double intervalPeak_ = 0;
};
