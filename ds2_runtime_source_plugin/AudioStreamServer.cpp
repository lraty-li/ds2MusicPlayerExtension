#include "pch.h"

#include "AudioStreamServer.h"

#include "AudioPacketProtocol.h"
#include "AudioRingBuffer.h"
#include "BrowserJacket.h"
#include "BrowserMetadata.h"
#include "PluginLog.h"
#include "WebSocketProtocol.h"

#include <ws2tcpip.h>

#include <cstdio>
#include <cstdint>
#include <mutex>
#include <vector>

namespace
{
constexpr uint16_t kPort = 47832;
constexpr uint32_t kMaxFrameBytes = 4 * 1024 * 1024;

std::mutex g_socketMutex;
HANDLE g_thread = nullptr;
HANDLE g_stopEvent = nullptr;
SOCKET g_listenSocket = INVALID_SOCKET;
SOCKET g_clientSocket = INVALID_SOCKET;

void Log(const char* text)
{
    PluginLog::Write(text);
}

bool ShouldStop()
{
    return g_stopEvent && WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0;
}

void CloseSocket(SOCKET& socket)
{
    if (socket != INVALID_SOCKET)
    {
        shutdown(socket, SD_BOTH);
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
}

void ReplaceSocket(SOCKET& target, SOCKET socket)
{
    std::lock_guard<std::mutex> lock(g_socketMutex);
    target = socket;
}

void CloseOwnedSocket(SOCKET& target, SOCKET& socket)
{
    bool owned = false;
    {
        std::lock_guard<std::mutex> lock(g_socketMutex);
        owned = target == socket;
        if (owned) target = INVALID_SOCKET;
    }
    if (owned) CloseSocket(socket);
    else socket = INVALID_SOCKET;
}

void PushPacket(const AudioPacketProtocol::Packet& packet)
{
    if (packet.format == AudioPacketProtocol::SampleFormat::Float32)
    {
        AudioRingBuffer::PushFloat32(packet.payload, packet.frames, packet.channels);
        return;
    }
    AudioRingBuffer::PushPcm16(packet.payload, packet.frames, packet.channels);
}

void LogStats(uint64_t packets, uint64_t frames, uint64_t drops,
    uint32_t maxPacketGapMs)
{
    const AudioRingBuffer::Stats stats = AudioRingBuffer::SnapshotStats(true);
    char line[512] = {};
    sprintf_s(line,
        "audio stats packets=%llu frames=%llu drops=%llu packetGapMaxMs=%u "
        "buffered=%u min=%u max=%u underruns=%llu lockMisses=%llu "
        "shortReads=%llu silenceFrames=%llu read=%llu/%llu pushed=%llu "
        "trimmed=%llu overwritten=%llu",
        static_cast<unsigned long long>(packets),
        static_cast<unsigned long long>(frames),
        static_cast<unsigned long long>(drops),
        maxPacketGapMs,
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
    Log(line);
}

void HandleClient(SOCKET socket)
{
    if (!WebSocketProtocol::Accept(socket)) return;
    Log("audio websocket connected");
    std::vector<uint8_t> payload(kMaxFrameBytes + 1);
    uint64_t packets = 0;
    uint64_t frames = 0;
    uint64_t drops = 0;
    uint64_t lastSeq = UINT64_MAX;
    uint64_t lastLogTick = GetTickCount64();
    uint64_t lastPacketTick = 0;
    uint32_t maxPacketGapMs = 0;
    AudioRingBuffer::SnapshotStats(true);
    while (!ShouldStop())
    {
        uint32_t payloadBytes = 0;
        uint8_t opcode = 0;
        if (!WebSocketProtocol::ReadFrame(socket, payload.data(),
            kMaxFrameBytes, payloadBytes, opcode)) break;
        if (payloadBytes == 0) continue;
        if (opcode == 0x1)
        {
            payload[payloadBytes] = 0;
            const char* text = reinterpret_cast<const char*>(payload.data());
            BrowserJacket::UpdateStatusFromJson(text);
            BrowserJacket::UpdateFromJson(text);
            BrowserMetadata::UpdateFromJson(text);
            continue;
        }
        if (opcode != 0x2) continue;

        AudioPacketProtocol::Packet packet = {};
        if (!AudioPacketProtocol::TryParse(payload.data(), payloadBytes, packet)) continue;

        const uint64_t now = GetTickCount64();
        if (lastPacketTick != 0)
        {
            const uint64_t gap = now - lastPacketTick;
            if (gap > maxPacketGapMs)
            {
                maxPacketGapMs = static_cast<uint32_t>(gap);
            }
        }
        lastPacketTick = now;

        if (lastSeq != UINT64_MAX && packet.sequence != lastSeq + 1)
        {
            drops += packet.sequence > lastSeq ? packet.sequence - lastSeq - 1 : 1;
        }
        lastSeq = packet.sequence;
        ++packets;
        frames += packet.frames;
        PushPacket(packet);

        if (PluginLog::Enabled() && now - lastLogTick >= 5000)
        {
            LogStats(packets, frames, drops, maxPacketGapMs);
            maxPacketGapMs = 0;
            lastLogTick = now;
        }
    }
    Log("audio websocket disconnected");
}

bool BindListen(SOCKET listener)
{
    BOOL reuse = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    return bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
        listen(listener, 1) == 0;
}

DWORD WINAPI ServerThread(LPVOID)
{
    WSADATA wsa = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
    Log("audio websocket server thread started");
    while (!ShouldStop())
    {
        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) break;
        ReplaceSocket(g_listenSocket, listener);
        if (!BindListen(listener))
        {
            Log("audio websocket listen failed");
            CloseOwnedSocket(g_listenSocket, listener);
            Sleep(1000);
            continue;
        }
        Log("audio websocket listening on 127.0.0.1:47832");
        SOCKET client = accept(listener, nullptr, nullptr);
        CloseOwnedSocket(g_listenSocket, listener);
        if (client == INVALID_SOCKET) continue;
        ReplaceSocket(g_clientSocket, client);
        HandleClient(client);
        CloseOwnedSocket(g_clientSocket, client);
    }
    Log("audio websocket server thread stopped");
    WSACleanup();
    return 0;
}
}

namespace AudioStreamServer
{
void Start()
{
    if (g_thread) return;
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, ServerThread, nullptr, 0, nullptr);
}

void Stop()
{
    if (g_stopEvent) SetEvent(g_stopEvent);
    {
        std::lock_guard<std::mutex> lock(g_socketMutex);
        CloseSocket(g_clientSocket);
        CloseSocket(g_listenSocket);
    }
    if (g_thread)
    {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_stopEvent)
    {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}

uint32_t Read(float* const* outputs, uint32_t frames, uint32_t channels)
{
    return AudioRingBuffer::Read(outputs, frames, channels);
}

bool SendControl(const char* json)
{
    SOCKET client = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(g_socketMutex);
        client = g_clientSocket;
    }
    if (client == INVALID_SOCKET || !json)
    {
        return false;
    }
    return WebSocketProtocol::SendTextFrame(client, json);
}

int ReadMetadataTitle(char* output, uint32_t outputBytes)
{
    return BrowserMetadata::ReadTitle(output, outputBytes);
}

int ReadMetadata(char* title, uint32_t titleBytes, char* artist, uint32_t artistBytes)
{
    return BrowserMetadata::Read(title, titleBytes, artist, artistBytes);
}
}
