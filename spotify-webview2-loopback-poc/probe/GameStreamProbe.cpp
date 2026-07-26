#include "WebSocketWire.h"

#include "GameAudioProtocol.h"
#include "ProbeAudioRing.h"

#include <Windows.h>
#include <conio.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace
{
constexpr uint16_t kPort = 47832;
std::mutex g_logMutex;
std::ofstream g_logFile;

void Log(const std::string& text)
{
    std::lock_guard lock(g_logMutex);
    std::cout << text << std::endl;
    g_logFile << text << std::endl;
}

SOCKET Listen()
{
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return INVALID_SOCKET;
    BOOL reuse = TRUE;
    setsockopt(
        listener,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse),
        sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kPort);
    InetPtonA(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(
            listener,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) != 0 ||
        listen(listener, 1) != 0)
    {
        WebSocketWire::Close(listener);
    }
    return listener;
}

void ConsumeLoop(
    ProbeAudioRing& ring,
    const std::atomic<bool>& stop)
{
    bool primed = false;
    auto next = std::chrono::steady_clock::now();
    while (!stop.load(std::memory_order_acquire))
    {
        if (!primed)
        {
            primed = ring.Ready();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            next = std::chrono::steady_clock::now();
            continue;
        }
        next += std::chrono::milliseconds(10);
        ring.Consume(GameAudioProtocol::kPacketFrames);
        const auto now = std::chrono::steady_clock::now();
        if (now > next + std::chrono::milliseconds(100))
        {
            next = now;
        }
        std::this_thread::sleep_until(next);
    }
}

std::string MetricsLine(
    uint64_t packets,
    uint64_t frames,
    uint64_t lastSequence,
    uint64_t gaps,
    uint64_t invalid,
    uint32_t maxPacketGapMs,
    const ProbeAudioStats& audio)
{
    std::ostringstream line;
    line << std::fixed << std::setprecision(7)
         << "PROBE_PCM packets=" << packets
         << " frames=" << frames
         << " lastSequence=" << lastSequence
         << " gaps=" << gaps
         << " invalid=" << invalid
         << " maxPacketGapMs=" << maxPacketGapMs
         << " buffered=" << audio.availableFrames
         << " min=" << audio.minAvailableFrames
         << " max=" << audio.maxAvailableFrames
         << " underruns=" << audio.underruns
         << " silenceFrames=" << audio.silenceFrames
         << " trimmed=" << audio.trimmedFrames
         << " overwritten=" << audio.overwrittenFrames
         << " rms=" << audio.rms
         << " peak=" << audio.peak;
    return line.str();
}

bool SendControl(SOCKET client, const char* command);

void ReceiveLoop(
    SOCKET client,
    ProbeAudioRing& ring,
    std::atomic<bool>& stop)
{
    uint64_t packets = 0;
    uint64_t frames = 0;
    uint64_t lastSequence = 0;
    uint64_t gaps = 0;
    uint64_t invalid = 0;
    bool haveSequence = false;
    uint32_t maxPacketGapMs = 0;
    auto lastPacket = std::chrono::steady_clock::time_point{};
    auto lastReport = std::chrono::steady_clock::now();
    while (!stop.load(std::memory_order_acquire))
    {
        WebSocketWire::Message message;
        const auto status = WebSocketWire::TryRead(
            client, true, 100, message);
        if (status == WebSocketWire::ReadStatus::Closed ||
            status == WebSocketWire::ReadStatus::Error)
        {
            Log("PROBE websocket disconnected");
            stop.store(true, std::memory_order_release);
            break;
        }
        if (status == WebSocketWire::ReadStatus::Message &&
            message.opcode == 0x1)
        {
            const std::string text(
                message.payload.begin(), message.payload.end());
            if (text.find("\"probeCommand\":\"pause\"") !=
                std::string::npos)
            {
                SendControl(client, "pause");
            }
            else if (text.find("\"probeCommand\":\"resume\"") !=
                std::string::npos)
            {
                SendControl(client, "resume");
            }
            else if (text.find("\"type\":\"metadata\"") !=
                std::string::npos)
            {
                Log("PROBE_METADATA " + text);
            }
            else if (text.find("\"type\":\"jacket_status\"") !=
                std::string::npos)
            {
                Log("PROBE_JACKET_STATUS " + text);
            }
            else if (text.find("\"type\":\"jacket\"") !=
                std::string::npos)
            {
                Log("PROBE_JACKET messageBytes=" +
                    std::to_string(text.size()));
            }
        }
        else if (status == WebSocketWire::ReadStatus::Message &&
                 message.opcode == 0x2)
        {
            GameAudioProtocol::PacketView packet;
            if (!GameAudioProtocol::DecodePcm16(
                    message.payload, packet) ||
                !ring.PushPcm16(
                    std::span(packet.payload, packet.payloadBytes),
                    packet.frames))
            {
                ++invalid;
            }
            else
            {
                const auto now = std::chrono::steady_clock::now();
                if (lastPacket.time_since_epoch().count() != 0)
                {
                    const auto gap = std::chrono::duration_cast<
                        std::chrono::milliseconds>(now - lastPacket).count();
                    maxPacketGapMs = std::max(
                        maxPacketGapMs,
                        static_cast<uint32_t>(gap));
                }
                lastPacket = now;
                if (haveSequence && packet.sequence != lastSequence + 1)
                {
                    gaps += packet.sequence > lastSequence
                        ? packet.sequence - lastSequence - 1
                        : 1;
                }
                haveSequence = true;
                lastSequence = packet.sequence;
                ++packets;
                frames += packet.frames;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - lastReport >= std::chrono::seconds(1))
        {
            Log(MetricsLine(
                packets,
                frames,
                lastSequence,
                gaps,
                invalid,
                maxPacketGapMs,
                ring.Snapshot(true)));
            maxPacketGapMs = 0;
            lastReport = now;
        }
    }
}

bool SendControl(SOCKET client, const char* command)
{
    const std::string json =
        std::string("{\"command\":\"") + command + "\"}";
    const bool sent = WebSocketWire::SendServerText(client, json);
    Log(std::string("PROBE_CONTROL ") + command +
        (sent ? " sent" : " failed"));
    return sent;
}
}

int main()
{
    g_logFile.open("game-stream-probe.log", std::ios::trunc);
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0)
    {
        Log("PROBE WSAStartup failed");
        return 1;
    }
    SOCKET listener = Listen();
    if (listener == INVALID_SOCKET)
    {
        Log("PROBE listen failed on 127.0.0.1:47832");
        WSACleanup();
        return 2;
    }
    Log("PROBE listening on ws://127.0.0.1:47832");
    SOCKET client = accept(listener, nullptr, nullptr);
    WebSocketWire::Close(listener);
    std::string error;
    if (client == INVALID_SOCKET ||
        !WebSocketWire::AcceptServer(client, error))
    {
        Log("PROBE handshake failed: " + error);
        WebSocketWire::Close(client);
        WSACleanup();
        return 3;
    }
    Log("PROBE connected; P=pause, R=resume, Q=quit");

    ProbeAudioRing ring;
    std::atomic<bool> stop{false};
    std::thread consumer(ConsumeLoop, std::ref(ring), std::cref(stop));
    std::thread receiver(
        ReceiveLoop,
        client,
        std::ref(ring),
        std::ref(stop));
    while (!stop.load(std::memory_order_acquire))
    {
        if (_kbhit())
        {
            const int key = std::tolower(_getch());
            if (key == 'p') SendControl(client, "pause");
            if (key == 'r') SendControl(client, "resume");
            if (key == 'q') stop.store(true, std::memory_order_release);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    WebSocketWire::Close(client);
    if (receiver.joinable()) receiver.join();
    if (consumer.joinable()) consumer.join();
    Log("PROBE stopped");
    WSACleanup();
    return 0;
}
