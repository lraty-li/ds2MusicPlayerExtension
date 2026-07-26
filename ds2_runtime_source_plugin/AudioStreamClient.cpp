#include "pch.h"

#include "AudioStreamClient.h"

#include "AudioPacketProtocol.h"
#include "AudioRingBuffer.h"
#include "AudioSourceArbiter.h"
#include "BrowserJacket.h"
#include "BrowserMetadata.h"
#include "PluginLog.h"
#include "WebSocketProtocol.h"

#include <cstdio>
#include <cstring>
#include <ws2tcpip.h>

namespace
{
bool HasType(const char* json, const char* compact, const char* spaced)
{
    return strstr(json, compact) || strstr(json, spaced);
}

std::string ReadJsonString(const char* json, const char* key)
{
    const char* at = strstr(json, key);
    if (!at) return {};
    at = strchr(at + strlen(key), ':');
    if (!at) return {};
    at = strchr(at, '"');
    if (!at) return {};
    ++at;
    std::string value;
    while (*at && *at != '"' && value.size() < 127)
    {
        if (*at == '\\' && at[1]) ++at;
        value.push_back(*at++);
    }
    return value;
}

void ResetStats(AudioStreamClient& client)
{
    client.packets = 0;
    client.frames = 0;
    client.drops = 0;
    client.lastSequence = UINT64_MAX;
    client.lastLogTick = GetTickCount64();
    client.lastPacketTick = 0;
    client.maxPacketGapMs = 0;
}

void LogStats(const AudioStreamClient& client)
{
    const AudioRingBuffer::Stats stats = AudioRingBuffer::SnapshotStats(true);
    char line[512] = {};
    sprintf_s(line,
        "audio stats source=%s packets=%llu frames=%llu drops=%llu "
        "packetGapMaxMs=%u buffered=%u min=%u max=%u underruns=%llu "
        "lockMisses=%llu shortReads=%llu silenceFrames=%llu "
        "read=%llu/%llu pushed=%llu trimmed=%llu overwritten=%llu",
        client.sourceKind.c_str(),
        static_cast<unsigned long long>(client.packets),
        static_cast<unsigned long long>(client.frames),
        static_cast<unsigned long long>(client.drops),
        client.maxPacketGapMs,
        stats.availableFrames,
        stats.minAvailableFrames,
        stats.maxAvailableFrames,
        static_cast<unsigned long long>(stats.underruns),
        static_cast<unsigned long long>(stats.lockMisses),
        static_cast<unsigned long long>(stats.shortReads),
        static_cast<unsigned long long>(stats.silenceFrames),
        static_cast<unsigned long long>(stats.readFramesCopied),
        static_cast<unsigned long long>(stats.readFramesRequested),
        static_cast<unsigned long long>(stats.pushFrames),
        static_cast<unsigned long long>(stats.trimmedFrames),
        static_cast<unsigned long long>(stats.overwrittenFrames));
    PluginLog::Write(line);
}

void PushPacket(const AudioPacketProtocol::Packet& packet)
{
    if (packet.format == AudioPacketProtocol::SampleFormat::Float32)
    {
        AudioRingBuffer::PushFloat32(
            packet.payload, packet.frames, packet.channels);
    }
    else
    {
        AudioRingBuffer::PushPcm16(
            packet.payload, packet.frames, packet.channels);
    }
}

bool HandleProtocolText(AudioStreamClient& client, const char* text)
{
    if (HasType(text, "\"type\":\"source_hello\"",
        "\"type\": \"source_hello\""))
    {
        client.protocolAware = true;
        client.sourceId = ReadJsonString(text, "\"sourceId\"");
        client.sourceKind = ReadJsonString(text, "\"sourceKind\"");
        AudioSourceArbiter::RegisterControlTarget(client.socket);
        char line[256] = {};
        sprintf_s(line, "audio source connected kind=\"%s\" id=\"%s\"",
            client.sourceKind.c_str(), client.sourceId.c_str());
        PluginLog::Write(line);
        return true;
    }
    if (!HasType(text, "\"type\":\"source_claim\"",
        "\"type\": \"source_claim\""))
    {
        return false;
    }

    client.protocolAware = true;
    const std::string id = ReadJsonString(text, "\"sourceId\"");
    const std::string kind = ReadJsonString(text, "\"sourceKind\"");
    const std::string reason = ReadJsonString(text, "\"reason\"");
    if (!id.empty()) client.sourceId = id;
    if (!kind.empty()) client.sourceKind = kind;
    if (AudioSourceArbiter::Claim(client.socket,
        client.sourceId.c_str(), client.sourceKind.c_str(), reason.c_str()))
    {
        ResetStats(client);
    }
    return true;
}

void HandleText(AudioStreamClient& client, const char* text)
{
    if (HandleProtocolText(client, text)) return;
    if (!AudioSourceArbiter::IsMetadataSource(client.socket))
    {
        if (HasType(text, "\"type\":\"metadata\"",
            "\"type\": \"metadata\""))
        {
            char line[256] = {};
            sprintf_s(line,
                "audio metadata ignored from inactive source "
                "kind=\"%s\" id=\"%s\" socket=%llu",
                client.sourceKind.c_str(), client.sourceId.c_str(),
                static_cast<unsigned long long>(client.socket));
            PluginLog::Write(line);
        }
        return;
    }
    BrowserJacket::UpdateStatusFromJson(text);
    BrowserJacket::UpdateFromJson(text);
    BrowserMetadata::UpdateFromJson(text);
}

void HandleBinary(AudioStreamClient& client,
    const uint8_t* data, uint32_t bytes)
{
    if (!AudioSourceArbiter::IsActive(client.socket))
    {
        if (client.protocolAware) return;
        client.sourceId = "legacy-websocket";
        client.sourceKind = "legacy";
        if (!AudioSourceArbiter::Claim(client.socket,
            client.sourceId.c_str(), client.sourceKind.c_str(), "first_pcm"))
        {
            return;
        }
        ResetStats(client);
    }

    AudioPacketProtocol::Packet packet = {};
    if (!AudioPacketProtocol::TryParse(data, bytes, packet)) return;
    const uint64_t now = GetTickCount64();
    if (client.lastPacketTick)
    {
        const uint64_t gap = now - client.lastPacketTick;
        if (gap > client.maxPacketGapMs)
        {
            client.maxPacketGapMs = static_cast<uint32_t>(gap);
        }
    }
    client.lastPacketTick = now;
    if (client.lastSequence != UINT64_MAX &&
        packet.sequence != client.lastSequence + 1)
    {
        client.drops += packet.sequence > client.lastSequence ?
            packet.sequence - client.lastSequence - 1 : 1;
    }
    client.lastSequence = packet.sequence;
    ++client.packets;
    client.frames += packet.frames;
    PushPacket(packet);
    if (PluginLog::Enabled() && now - client.lastLogTick >= 5000)
    {
        LogStats(client);
        client.maxPacketGapMs = 0;
        client.lastLogTick = now;
    }
}
}

namespace AudioStreamClientIo
{
bool Accept(AudioStreamClient& client, SOCKET socket, uint32_t maxFrameBytes)
{
    client.socket = socket;
    client.payload.resize(static_cast<size_t>(maxFrameBytes) + 1);
    const BOOL noDelay = TRUE;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
        reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    AudioSourceArbiter::AddClient(socket);
    if (WebSocketProtocol::Accept(socket))
    {
        ResetStats(client);
        PluginLog::Write("audio websocket client connected");
        return true;
    }
    AudioSourceArbiter::RemoveClient(socket);
    closesocket(socket);
    client.socket = INVALID_SOCKET;
    return false;
}

bool ReadAndProcess(AudioStreamClient& client, uint32_t maxFrameBytes)
{
    uint32_t payloadBytes = 0;
    uint8_t opcode = 0;
    if (!WebSocketProtocol::ReadFrame(client.socket, client.payload.data(),
        maxFrameBytes, payloadBytes, opcode))
    {
        return false;
    }
    if (!payloadBytes) return true;
    if (opcode == 0x1)
    {
        client.payload[payloadBytes] = 0;
        HandleText(client,
            reinterpret_cast<const char*>(client.payload.data()));
    }
    else if (opcode == 0x2)
    {
        HandleBinary(client, client.payload.data(), payloadBytes);
    }
    return true;
}

void Close(AudioStreamClient& client)
{
    if (client.socket == INVALID_SOCKET) return;
    AudioSourceArbiter::RemoveClient(client.socket);
    shutdown(client.socket, SD_BOTH);
    closesocket(client.socket);
    client.socket = INVALID_SOCKET;
    PluginLog::Write("audio websocket client disconnected");
}
}
