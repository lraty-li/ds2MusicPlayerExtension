#include "PcmStreamReceiver.h"
#include "PcmChunkCodec.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace
{
constexpr ULONGLONG kReportIntervalMs = 1000;

std::wstring JsonText(std::wstring_view text)
{
    std::wstring result;
    result.reserve(text.size());
    for (const wchar_t character : text)
    {
        if (character == L'\\' || character == L'"')
        {
            result.push_back(L'\\');
        }
        result.push_back(character);
    }
    return result;
}
}

bool PcmStreamReceiver::HandleMessage(
    std::wstring_view message,
    std::wstring& metricsJson)
{
    metricsJson.clear();
    if (!IsPcmChunkMessage(message))
    {
        return false;
    }

    DecodedPcmChunk chunk;
    std::wstring decodeError;
    if (!DecodePcmChunk(message, chunk, decodeError))
    {
        RecordInvalidChunk(
            L"base64-webmessage", decodeError, metricsJson);
        return true;
    }
    HandleChunk(
        chunk, L"base64-webmessage", metricsJson);
    return true;
}

void PcmStreamReceiver::HandleChunk(
    const DecodedPcmChunk& chunk,
    std::wstring_view transport,
    std::wstring& metricsJson)
{
    metricsJson.clear();
    const ULONGLONG now = GetTickCount64();
    if (streamId_ != chunk.streamId)
    {
        ResetStream(
            chunk.streamId,
            transport,
            chunk.sampleRate,
            chunk.channels,
            now);
    }
    transport_.assign(transport);
    if (sampleRate_ != chunk.sampleRate || channels_ != chunk.channels)
    {
        RecordInvalidChunk(
            transport, L"format-changed", metricsJson);
        return;
    }

    if (totalChunks_ > 0)
    {
        if (chunk.sequence > expectedSequence_)
        {
            sequenceGaps_ += chunk.sequence - expectedSequence_;
        }
        else if (chunk.sequence < expectedSequence_)
        {
            ++outOfOrder_;
        }
    }
    else if (chunk.sequence > 0)
    {
        sequenceGaps_ += chunk.sequence;
    }
    expectedSequence_ = chunk.sequence + 1;
    lastSequence_ = chunk.sequence;

    for (size_t offset = 0; offset < chunk.bytes.size(); offset += 2)
    {
        int16_t sample = 0;
        std::memcpy(
            &sample, chunk.bytes.data() + offset, sizeof(sample));
        const double normalized = static_cast<double>(sample) / 32768.0;
        intervalSumSquares_ += normalized * normalized;
        intervalPeak_ = std::max(intervalPeak_, std::abs(normalized));
        intervalNonzero_ += sample != 0 ? 1 : 0;
        ++intervalSamples_;
    }
    for (const uint8_t byte : chunk.bytes)
    {
        checksum_ ^= byte;
        checksum_ *= 1099511628211ull;
    }
    ++totalChunks_;
    totalFrames_ += chunk.frames;
    totalBytes_ += chunk.bytes.size();
    intervalFrames_ += chunk.frames;
    intervalBytes_ += chunk.bytes.size();
    lastError_.clear();

    const bool warmingUp = totalChunks_ == 1;
    if (warmingUp || now - intervalStarted_ >= kReportIntervalMs)
    {
        metricsJson = BuildMetricsJson(now, warmingUp);
        ResetInterval(now);
    }
}

void PcmStreamReceiver::RecordInvalidChunk(
    std::wstring_view transport,
    std::wstring_view error,
    std::wstring& metricsJson)
{
    const ULONGLONG now = GetTickCount64();
    transport_.assign(transport);
    ++invalidChunks_;
    lastError_.assign(error);
    metricsJson = BuildMetricsJson(now, false);
    ResetInterval(now);
}

void PcmStreamReceiver::ResetStream(
    std::wstring_view streamId,
    std::wstring_view transport,
    uint32_t sampleRate,
    uint32_t channels,
    ULONGLONG now)
{
    streamId_.assign(streamId);
    transport_.assign(transport);
    sampleRate_ = sampleRate;
    channels_ = channels;
    expectedSequence_ = 0;
    lastSequence_ = 0;
    totalChunks_ = 0;
    totalFrames_ = 0;
    totalBytes_ = 0;
    sequenceGaps_ = 0;
    outOfOrder_ = 0;
    invalidChunks_ = 0;
    checksum_ = 14695981039346656037ull;
    lastError_.clear();
    ResetInterval(now);
}

std::wstring PcmStreamReceiver::BuildMetricsJson(
    ULONGLONG now,
    bool warmingUp)
{
    const ULONGLONG elapsed = now - intervalStarted_;
    const double seconds = elapsed > 0 ? elapsed / 1000.0 : 0;
    const double framesPerSecond =
        !warmingUp && seconds > 0 ? intervalFrames_ / seconds : 0;
    const double bytesPerSecond =
        !warmingUp && seconds > 0 ? intervalBytes_ / seconds : 0;
    const double rms = intervalSamples_ > 0
        ? std::sqrt(
            static_cast<double>(intervalSumSquares_ / intervalSamples_))
        : 0;
    const double nonzeroRatio = intervalSamples_ > 0
        ? static_cast<double>(intervalNonzero_) / intervalSamples_
        : 0;
    const bool continuous =
        totalChunks_ >= 10 &&
        sequenceGaps_ == 0 &&
        outOfOrder_ == 0 &&
        invalidChunks_ == 0;
    const bool throughputOk =
        seconds >= 0.5 &&
        framesPerSecond >= sampleRate_ * 0.75 &&
        framesPerSecond <= sampleRate_ * 1.25;
    const bool hasPcm = intervalPeak_ > 0.0001 && nonzeroRatio > 0.001;

    std::wostringstream json;
    json << std::fixed << std::setprecision(7)
         << L"{\"type\":\"pcm-bridge-metrics\""
         << L",\"active\":" << (!streamId_.empty() ? L"true" : L"false")
         << L",\"streamId\":\"" << JsonText(streamId_) << L"\""
         << L",\"transport\":\"" << JsonText(transport_) << L"\""
         << L",\"sampleRate\":" << sampleRate_
         << L",\"channels\":" << channels_
         << L",\"lastSequence\":" << lastSequence_
         << L",\"totalChunks\":" << totalChunks_
         << L",\"totalFrames\":" << totalFrames_
         << L",\"totalBytes\":" << totalBytes_
         << L",\"sequenceGaps\":" << sequenceGaps_
         << L",\"outOfOrder\":" << outOfOrder_
         << L",\"invalidChunks\":" << invalidChunks_
         << L",\"intervalMs\":" << elapsed
         << L",\"framesPerSecond\":" << framesPerSecond
         << L",\"bytesPerSecond\":" << bytesPerSecond
         << L",\"rms\":" << rms
         << L",\"peak\":" << intervalPeak_
         << L",\"nonzeroRatio\":" << nonzeroRatio
         << L",\"continuous\":" << (continuous ? L"true" : L"false")
         << L",\"throughputOk\":" << (throughputOk ? L"true" : L"false")
         << L",\"hasPcm\":" << (hasPcm ? L"true" : L"false")
         << L",\"checksum\":\"" << std::hex << std::setw(16)
         << std::setfill(L'0') << checksum_ << L"\""
         << L",\"error\":\"" << JsonText(lastError_) << L"\"}";
    return json.str();
}

void PcmStreamReceiver::ResetInterval(ULONGLONG now)
{
    intervalStarted_ = now;
    intervalFrames_ = 0;
    intervalBytes_ = 0;
    intervalSamples_ = 0;
    intervalNonzero_ = 0;
    intervalSumSquares_ = 0;
    intervalPeak_ = 0;
}
