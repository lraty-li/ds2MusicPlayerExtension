#include "PcmSharedRing.h"

#include <array>
#include <cstring>
#include <limits>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr std::wstring_view kCommitPrefix = L"pcm-ring-v1|";

bool ParseUnsigned(std::wstring_view text, uint64_t& value)
{
    if (text.empty())
    {
        return false;
    }
    uint64_t parsed = 0;
    for (const wchar_t character : text)
    {
        if (character < L'0' || character > L'9')
        {
            return false;
        }
        const uint64_t digit = static_cast<uint64_t>(character - L'0');
        if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10)
        {
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

bool ParseCommit(
    std::wstring_view message,
    std::wstring_view& streamId,
    uint64_t& sequence,
    uint64_t& slot)
{
    std::array<std::wstring_view, 4> fields{};
    size_t start = 0;
    for (size_t index = 0; index + 1 < fields.size(); ++index)
    {
        const size_t separator = message.find(L'|', start);
        if (separator == std::wstring_view::npos)
        {
            return false;
        }
        fields[index] = message.substr(start, separator - start);
        start = separator + 1;
    }
    fields.back() = message.substr(start);
    if (fields[0] != L"pcm-ring-v1" ||
        fields[1].empty() ||
        fields[1].size() > 96 ||
        !ParseUnsigned(fields[2], sequence) ||
        !ParseUnsigned(fields[3], slot))
    {
        return false;
    }
    streamId = fields[1];
    return true;
}

uint32_t Checksum(const BYTE* bytes, size_t length)
{
    uint32_t checksum = 2166136261u;
    for (size_t index = 0; index < length; ++index)
    {
        checksum ^= bytes[index];
        checksum *= 16777619u;
    }
    return checksum;
}
}

HRESULT PcmSharedRing::Initialize(
    ICoreWebView2Environment* environment)
{
    Close();
    if (!environment)
    {
        return E_POINTER;
    }
    ComPtr<ICoreWebView2Environment12> sharedEnvironment;
    HRESULT result = environment->QueryInterface(
        IID_PPV_ARGS(&sharedEnvironment));
    if (FAILED(result))
    {
        return result;
    }
    result = sharedEnvironment->CreateSharedBuffer(
        kBufferBytes, &buffer_);
    if (FAILED(result))
    {
        return result;
    }
    result = buffer_->get_Buffer(&bytes_);
    if (FAILED(result) || !bytes_)
    {
        Close();
        return FAILED(result) ? result : E_POINTER;
    }
    std::memset(bytes_, 0, kBufferBytes);
    Write32(0, kRingMagic);
    Write32(4, kVersion);
    Write32(8, static_cast<uint32_t>(kHeaderBytes));
    Write32(12, static_cast<uint32_t>(kSlotCount));
    Write32(16, static_cast<uint32_t>(kSlotBytes));
    Write32(20, static_cast<uint32_t>(kSlotHeaderBytes));
    Write32(24, static_cast<uint32_t>(kPayloadCapacity));

    std::wostringstream json;
    json << L"{\"type\":\"ds2-pcm-ring-v1\""
         << L",\"version\":" << kVersion
         << L",\"bufferBytes\":" << kBufferBytes
         << L",\"headerBytes\":" << kHeaderBytes
         << L",\"slotCount\":" << kSlotCount
         << L",\"slotBytes\":" << kSlotBytes
         << L",\"slotHeaderBytes\":" << kSlotHeaderBytes
         << L",\"payloadCapacity\":" << kPayloadCapacity
         << L",\"slotMagic\":" << kSlotMagic
         << L",\"commitXor\":" << kCommitXor << L"}";
    descriptorJson_ = json.str();
    return S_OK;
}

void PcmSharedRing::Close()
{
    bytes_ = nullptr;
    descriptorJson_.clear();
    if (buffer_)
    {
        buffer_->Close();
        buffer_.Reset();
    }
}

bool PcmSharedRing::IsReady() const noexcept
{
    return buffer_.Get() != nullptr &&
        bytes_ != nullptr &&
        !descriptorJson_.empty();
}

ICoreWebView2SharedBuffer* PcmSharedRing::Buffer() const noexcept
{
    return buffer_.Get();
}

const std::wstring& PcmSharedRing::DescriptorJson() const noexcept
{
    return descriptorJson_;
}

bool PcmSharedRing::HandleCommit(
    std::wstring_view message,
    DecodedPcmChunk& chunk,
    std::wstring& error)
{
    if (!message.starts_with(kCommitPrefix))
    {
        return false;
    }
    std::wstring_view streamId;
    uint64_t sequence = 0;
    uint64_t slotValue = 0;
    if (!IsReady() ||
        !ParseCommit(message, streamId, sequence, slotValue) ||
        slotValue >= kSlotCount ||
        slotValue != sequence % kSlotCount)
    {
        error = L"ring-commit-schema";
        return true;
    }

    const size_t slot = static_cast<size_t>(slotValue);
    const size_t offset = kHeaderBytes + slot * kSlotBytes;
    MemoryBarrier();
    const uint32_t sequence32 = static_cast<uint32_t>(sequence);
    const uint32_t frames = Read32(offset + 12);
    const uint32_t sampleRate = Read32(offset + 16);
    const uint32_t channels = Read32(offset + 20);
    const uint32_t payloadBytes = Read32(offset + 24);
    const uint32_t expectedChecksum = Read32(offset + 28);
    const uint32_t commit = Read32(offset + 32);
    const uint64_t expectedBytes =
        static_cast<uint64_t>(frames) * channels * sizeof(int16_t);
    if (Read32(offset) != kSlotMagic ||
        Read32(offset + 4) != kVersion ||
        Read32(offset + 8) != sequence32 ||
        commit != (sequence32 ^ kCommitXor) ||
        sampleRate < 8000 || sampleRate > 192000 ||
        channels == 0 || channels > 8 ||
        frames == 0 || frames > 48000 ||
        payloadBytes > kPayloadCapacity ||
        expectedBytes != payloadBytes)
    {
        ResetSlot(slot);
        error = L"ring-slot-header";
        return true;
    }

    const BYTE* payload = bytes_ + offset + kSlotHeaderBytes;
    if (Checksum(payload, payloadBytes) != expectedChecksum)
    {
        ResetSlot(slot);
        error = L"ring-slot-checksum";
        return true;
    }
    chunk.streamId.assign(streamId);
    chunk.sequence = sequence;
    chunk.sampleRate = sampleRate;
    chunk.channels = channels;
    chunk.frames = frames;
    chunk.bytes.assign(payload, payload + payloadBytes);
    ResetSlot(slot);
    error.clear();
    return true;
}

uint32_t PcmSharedRing::Read32(size_t offset) const
{
    uint32_t value = 0;
    std::memcpy(&value, bytes_ + offset, sizeof(value));
    return value;
}

void PcmSharedRing::Write32(size_t offset, uint32_t value)
{
    std::memcpy(bytes_ + offset, &value, sizeof(value));
}

void PcmSharedRing::ResetSlot(size_t slot)
{
    const size_t commitOffset =
        kHeaderBytes + slot * kSlotBytes + 32;
    MemoryBarrier();
    Write32(commitOffset, 0);
    MemoryBarrier();
}
