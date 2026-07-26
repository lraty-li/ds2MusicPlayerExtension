#include "WebSocketWire.h"

#include <bcrypt.h>

#include <array>
#include <cstring>

namespace
{
constexpr uint32_t kMaxPayloadBytes = 4 * 1024 * 1024;

bool SendAll(SOCKET socket, const void* data, size_t bytes)
{
    const auto* cursor = static_cast<const char*>(data);
    size_t sentBytes = 0;
    while (sentBytes < bytes)
    {
        const int sent = send(
            socket,
            cursor + sentBytes,
            static_cast<int>(bytes - sentBytes),
            0);
        if (sent <= 0)
        {
            return false;
        }
        sentBytes += static_cast<size_t>(sent);
    }
    return true;
}

bool ReceiveAll(SOCKET socket, void* data, size_t bytes)
{
    auto* cursor = static_cast<char*>(data);
    size_t receivedBytes = 0;
    while (receivedBytes < bytes)
    {
        const int received = recv(
            socket,
            cursor + receivedBytes,
            static_cast<int>(bytes - receivedBytes),
            0);
        if (received <= 0)
        {
            return false;
        }
        receivedBytes += static_cast<size_t>(received);
    }
    return true;
}

bool SendFrame(
    SOCKET socket,
    uint8_t opcode,
    std::span<const uint8_t> payload,
    bool masked)
{
    if (payload.size() > kMaxPayloadBytes)
    {
        return false;
    }
    std::array<uint8_t, 14> header{};
    size_t headerBytes = 2;
    header[0] = static_cast<uint8_t>(0x80 | opcode);
    const uint8_t maskBit = masked ? 0x80 : 0;
    if (payload.size() < 126)
    {
        header[1] = static_cast<uint8_t>(maskBit | payload.size());
    }
    else if (payload.size() <= 0xFFFF)
    {
        header[1] = static_cast<uint8_t>(maskBit | 126);
        header[2] = static_cast<uint8_t>(payload.size() >> 8);
        header[3] = static_cast<uint8_t>(payload.size());
        headerBytes = 4;
    }
    else
    {
        header[1] = static_cast<uint8_t>(maskBit | 127);
        for (size_t index = 0; index < 8; ++index)
        {
            header[2 + index] = static_cast<uint8_t>(
                payload.size() >> ((7 - index) * 8));
        }
        headerBytes = 10;
    }
    std::vector<uint8_t> body(payload.begin(), payload.end());
    if (masked)
    {
        std::array<uint8_t, 4> mask{};
        if (BCryptGenRandom(
                nullptr,
                mask.data(),
                static_cast<ULONG>(mask.size()),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        {
            return false;
        }
        std::memcpy(header.data() + headerBytes, mask.data(), mask.size());
        headerBytes += mask.size();
        for (size_t index = 0; index < body.size(); ++index)
        {
            body[index] ^= mask[index & 3];
        }
    }
    return SendAll(socket, header.data(), headerBytes) &&
        SendAll(socket, body.data(), body.size());
}

uint64_t ReadNetworkInteger(const uint8_t* bytes, size_t count)
{
    uint64_t value = 0;
    for (size_t index = 0; index < count; ++index)
    {
        value = (value << 8) | bytes[index];
    }
    return value;
}
}

namespace WebSocketWire
{
bool SendClientBinary(
    SOCKET socket,
    std::span<const uint8_t> payload)
{
    return SendFrame(socket, 0x2, payload, true);
}

bool SendClientText(SOCKET socket, std::string_view text)
{
    return SendFrame(
        socket,
        0x1,
        std::span(
            reinterpret_cast<const uint8_t*>(text.data()),
            text.size()),
        true);
}

bool SendServerText(SOCKET socket, std::string_view text)
{
    return SendFrame(
        socket,
        0x1,
        std::span(
            reinterpret_cast<const uint8_t*>(text.data()),
            text.size()),
        false);
}

ReadStatus TryRead(
    SOCKET socket,
    bool peerFramesAreMasked,
    uint32_t timeoutMs,
    Message& message)
{
    message = {};
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(socket, &readable);
    timeval timeout{
        static_cast<long>(timeoutMs / 1000),
        static_cast<long>((timeoutMs % 1000) * 1000)};
    const int ready = select(0, &readable, nullptr, nullptr, &timeout);
    if (ready == 0) return ReadStatus::NoData;
    if (ready < 0) return ReadStatus::Error;

    std::array<uint8_t, 2> header{};
    if (!ReceiveAll(socket, header.data(), header.size()))
    {
        return ReadStatus::Closed;
    }
    const bool masked = (header[1] & 0x80) != 0;
    uint64_t length = header[1] & 0x7F;
    if (masked != peerFramesAreMasked)
    {
        return ReadStatus::Error;
    }
    if (length == 126)
    {
        std::array<uint8_t, 2> extended{};
        if (!ReceiveAll(socket, extended.data(), extended.size()))
            return ReadStatus::Closed;
        length = ReadNetworkInteger(extended.data(), extended.size());
    }
    else if (length == 127)
    {
        std::array<uint8_t, 8> extended{};
        if (!ReceiveAll(socket, extended.data(), extended.size()))
            return ReadStatus::Closed;
        length = ReadNetworkInteger(extended.data(), extended.size());
    }
    if (length > kMaxPayloadBytes)
    {
        return ReadStatus::Error;
    }
    std::array<uint8_t, 4> mask{};
    if (masked && !ReceiveAll(socket, mask.data(), mask.size()))
    {
        return ReadStatus::Closed;
    }
    message.opcode = header[0] & 0x0F;
    message.payload.resize(static_cast<size_t>(length));
    if (length > 0 &&
        !ReceiveAll(socket, message.payload.data(), message.payload.size()))
    {
        return ReadStatus::Closed;
    }
    if (masked)
    {
        for (size_t index = 0; index < message.payload.size(); ++index)
        {
            message.payload[index] ^= mask[index & 3];
        }
    }
    return message.opcode == 0x8
        ? ReadStatus::Closed
        : ReadStatus::Message;
}

void Close(SOCKET& socket)
{
    if (socket == INVALID_SOCKET)
    {
        return;
    }
    shutdown(socket, SD_BOTH);
    closesocket(socket);
    socket = INVALID_SOCKET;
}
}
