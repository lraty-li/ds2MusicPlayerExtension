#include "pch.h"

#include "AudioStreamServer.h"

#include "AudioRingBuffer.h"
#include "BrowserMetadata.h"
#include "PluginLog.h"
#include "WebSocketProtocol.h"

#include <ws2tcpip.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace
{
constexpr uint16_t kPort = 47832;
constexpr uint32_t kMagic = 0x44533241;
constexpr uint16_t kPacketVersion = 1;
constexpr uint32_t kExpectedRate = 48000;
constexpr uint32_t kMaxPacketBytes = 65536;
constexpr uint16_t kMaxChannels = 2;

struct PcmPacket
{
    const uint8_t* pcm = nullptr;
    uint16_t channels = 0;
    uint32_t frames = 0;
    uint64_t sequence = 0;
};

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

uint32_t ReadLE32(const uint8_t* data)
{
    uint32_t value = 0;
    memcpy(&value, data, sizeof(value));
    return value;
}

uint16_t ReadLE16(const uint8_t* data)
{
    uint16_t value = 0;
    memcpy(&value, data, sizeof(value));
    return value;
}

uint64_t ReadLE64(const uint8_t* data)
{
    uint64_t value = 0;
    memcpy(&value, data, sizeof(value));
    return value;
}

bool TryParsePcmPacket(const uint8_t* packet, uint32_t packetBytes,
    PcmPacket& parsed)
{
    parsed = {};
    if (!packet || packetBytes < 28 || ReadLE32(packet) != kMagic) return false;
    const uint16_t version = ReadLE16(packet + 4);
    const uint16_t channels = ReadLE16(packet + 6);
    const uint32_t rate = ReadLE32(packet + 8);
    const uint32_t frames = ReadLE32(packet + 12);
    const uint64_t sequence = ReadLE64(packet + 16);
    const uint32_t pcmBytes = ReadLE32(packet + 24);
    const uint64_t expectedBytes =
        static_cast<uint64_t>(frames) * channels * sizeof(int16_t);
    if (version != kPacketVersion || rate != kExpectedRate) return false;
    if (channels == 0 || channels > kMaxChannels || frames == 0) return false;
    if (pcmBytes != packetBytes - 28 || expectedBytes != pcmBytes) return false;

    parsed.pcm = packet + 28;
    parsed.channels = channels;
    parsed.frames = frames;
    parsed.sequence = sequence;
    return true;
}

void LogStats(uint64_t packets, uint64_t frames, uint64_t drops)
{
    char line[192] = {};
    sprintf_s(line, "ws pcm packets=%llu frames=%llu drops=%llu buffered=%u underruns=%llu",
        static_cast<unsigned long long>(packets),
        static_cast<unsigned long long>(frames),
        static_cast<unsigned long long>(drops),
        AudioRingBuffer::AvailableFrames(),
        static_cast<unsigned long long>(AudioRingBuffer::Underruns()));
    Log(line);
}

void HandleClient(SOCKET socket)
{
    if (!WebSocketProtocol::Accept(socket)) return;
    Log("audio websocket connected");
    uint8_t payload[kMaxPacketBytes] = {};
    uint64_t packets = 0;
    uint64_t frames = 0;
    uint64_t drops = 0;
    uint64_t lastSeq = UINT64_MAX;
    uint64_t lastLogTick = GetTickCount64();
    while (!ShouldStop())
    {
        uint32_t payloadBytes = 0;
        uint8_t opcode = 0;
        if (!WebSocketProtocol::ReadFrame(socket, payload,
            sizeof(payload), payloadBytes, opcode)) break;
        if (payloadBytes == 0) continue;
        if (opcode == 0x1)
        {
            payload[min(payloadBytes, kMaxPacketBytes - 1)] = 0;
            BrowserMetadata::UpdateFromJson(reinterpret_cast<const char*>(payload));
            continue;
        }
        if (opcode != 0x2) continue;

        PcmPacket packet = {};
        if (!TryParsePcmPacket(payload, payloadBytes, packet)) continue;

        if (lastSeq != UINT64_MAX && packet.sequence != lastSeq + 1)
        {
            drops += packet.sequence > lastSeq ? packet.sequence - lastSeq - 1 : 1;
        }
        lastSeq = packet.sequence;
        ++packets;
        frames += packet.frames;
        AudioRingBuffer::PushPcm16(packet.pcm, packet.frames, packet.channels);

        const uint64_t now = GetTickCount64();
        if (now - lastLogTick >= 5000)
        {
            LogStats(packets, frames, drops);
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
