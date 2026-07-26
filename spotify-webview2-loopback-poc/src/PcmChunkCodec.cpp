#include "PcmChunkCodec.h"

#include <array>
#include <limits>

namespace
{
constexpr std::wstring_view kPrefix = L"pcm-v1|";

struct PcmChunkFields
{
    std::wstring_view streamId;
    uint64_t sequence = 0;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint32_t frames = 0;
    std::wstring_view payload;
};

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

bool SplitMessage(
    std::wstring_view message,
    std::array<std::wstring_view, 7>& fields)
{
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
    return true;
}

bool ParseFields(
    std::wstring_view message,
    PcmChunkFields& chunk)
{
    std::array<std::wstring_view, 7> fields{};
    uint64_t sampleRate = 0;
    uint64_t channels = 0;
    uint64_t frames = 0;
    if (!SplitMessage(message, fields) ||
        fields[0] != L"pcm-v1" ||
        fields[1].empty() ||
        fields[1].size() > 96 ||
        !ParseUnsigned(fields[2], chunk.sequence) ||
        !ParseUnsigned(fields[3], sampleRate) ||
        !ParseUnsigned(fields[4], channels) ||
        !ParseUnsigned(fields[5], frames) ||
        sampleRate < 8000 || sampleRate > 192000 ||
        channels == 0 || channels > 8 ||
        frames == 0 || frames > 48000 ||
        fields[6].empty() || fields[6].size() > 1048576)
    {
        return false;
    }
    chunk.streamId = fields[1];
    chunk.sampleRate = static_cast<uint32_t>(sampleRate);
    chunk.channels = static_cast<uint32_t>(channels);
    chunk.frames = static_cast<uint32_t>(frames);
    chunk.payload = fields[6];
    return true;
}

int Base64Value(wchar_t character)
{
    if (character >= L'A' && character <= L'Z')
    {
        return character - L'A';
    }
    if (character >= L'a' && character <= L'z')
    {
        return character - L'a' + 26;
    }
    if (character >= L'0' && character <= L'9')
    {
        return character - L'0' + 52;
    }
    if (character == L'+')
    {
        return 62;
    }
    return character == L'/' ? 63 : -1;
}

bool DecodeBase64(
    std::wstring_view text,
    std::vector<uint8_t>& output)
{
    if (text.size() % 4 != 0)
    {
        return false;
    }
    output.clear();
    output.reserve((text.size() / 4) * 3);
    for (size_t offset = 0; offset < text.size(); offset += 4)
    {
        const int first = Base64Value(text[offset]);
        const int second = Base64Value(text[offset + 1]);
        const bool thirdPadding = text[offset + 2] == L'=';
        const bool fourthPadding = text[offset + 3] == L'=';
        const int third = thirdPadding ? 0 : Base64Value(text[offset + 2]);
        const int fourth = fourthPadding ? 0 : Base64Value(text[offset + 3]);
        const bool finalGroup = offset + 4 == text.size();
        if (first < 0 || second < 0 || third < 0 || fourth < 0 ||
            (thirdPadding && !fourthPadding) ||
            ((thirdPadding || fourthPadding) && !finalGroup))
        {
            return false;
        }
        output.push_back(static_cast<uint8_t>((first << 2) | (second >> 4)));
        if (!thirdPadding)
        {
            output.push_back(
                static_cast<uint8_t>((second << 4) | (third >> 2)));
        }
        if (!fourthPadding)
        {
            output.push_back(
                static_cast<uint8_t>((third << 6) | fourth));
        }
    }
    return true;
}
}

bool IsPcmChunkMessage(std::wstring_view message)
{
    return message.starts_with(kPrefix);
}

bool DecodePcmChunk(
    std::wstring_view message,
    DecodedPcmChunk& chunk,
    std::wstring& error)
{
    PcmChunkFields fields;
    if (!ParseFields(message, fields))
    {
        error = L"message-schema";
        return false;
    }
    const uint64_t expectedBytes =
        static_cast<uint64_t>(fields.frames) *
        fields.channels * sizeof(int16_t);
    if (expectedBytes > std::numeric_limits<size_t>::max() ||
        !DecodeBase64(fields.payload, chunk.bytes) ||
        chunk.bytes.size() != static_cast<size_t>(expectedBytes))
    {
        error = L"payload-size-or-base64";
        return false;
    }
    chunk.streamId.assign(fields.streamId);
    chunk.sequence = fields.sequence;
    chunk.sampleRate = fields.sampleRate;
    chunk.channels = fields.channels;
    chunk.frames = fields.frames;
    error.clear();
    return true;
}
