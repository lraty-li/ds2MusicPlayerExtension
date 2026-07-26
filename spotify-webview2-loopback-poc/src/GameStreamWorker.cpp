#include "WebSocketWire.h"

#include "GameStreamClientInternal.h"
#include "GameAudioProtocol.h"

#include <chrono>
#include <string>

namespace
{
constexpr uint16_t kGameStreamPort = 47832;
constexpr auto kRetryDelay = std::chrono::milliseconds(500);

std::wstring Widen(std::string_view text)
{
    return std::wstring(text.begin(), text.end());
}
}

bool GameStreamClient::Impl::ShouldStop() const
{
    std::lock_guard lock(mutex_);
    return stop_;
}

void GameStreamClient::Impl::Notify(GameStreamEvent event) const
{
    if (notifyWindow_)
    {
        PostMessageW(
            notifyWindow_,
            kGameStreamEventMessage,
            static_cast<WPARAM>(event),
            0);
    }
}

void GameStreamClient::Impl::SetConnected(
    bool connected,
    std::wstring error)
{
    {
        std::lock_guard lock(mutex_);
        connected_ = connected;
        lastError_ = std::move(error);
        if (connected)
        {
            ++connections_;
            packets_.clear();
            packetsSent_ = 0;
            framesSent_ = 0;
            pcmBytesSent_ = 0;
            droppedPackets_ = 0;
            sendErrors_ = 0;
            maxQueueDepth_ = 0;
        }
    }
    Notify(GameStreamEvent::StateChanged);
}

void GameStreamClient::Impl::RecordSent()
{
    std::lock_guard lock(mutex_);
    ++packetsSent_;
    framesSent_ += GameAudioProtocol::kPacketFrames;
    pcmBytesSent_ += GameAudioProtocol::kPcm16PayloadBytes;
    lastError_.clear();
}

void GameStreamClient::Impl::RecordSendFailure()
{
    std::lock_guard lock(mutex_);
    ++sendErrors_;
    connected_ = false;
    lastError_ = L"websocket send failed";
}

void GameStreamClient::Impl::HandleControl(
    uint8_t opcode,
    const std::vector<uint8_t>& payload)
{
    if (opcode != 0x1) return;
    const std::string text(payload.begin(), payload.end());
    GameStreamEvent event{};
    {
        std::lock_guard lock(mutex_);
        if (text.find("\"command\"") == std::string::npos)
        {
            return;
        }
        if (text.find("\"pause\"") != std::string::npos)
        {
            ++pauseCommands_;
            event = GameStreamEvent::Pause;
        }
        else if (text.find("\"resume\"") != std::string::npos)
        {
            ++resumeCommands_;
            event = GameStreamEvent::Resume;
        }
        else
        {
            return;
        }
    }
    Notify(event);
}

bool GameStreamClient::Impl::SendNext(UINT_PTR socketValue)
{
    std::vector<uint8_t> packet;
    std::string diagnosticControl;
    {
        std::unique_lock lock(mutex_);
        wake_.wait_for(
            lock,
            std::chrono::milliseconds(10),
            [this]
            {
                return stop_ ||
                    !diagnosticControls_.empty() ||
                    !packets_.empty();
            });
        if (stop_) return false;
        if (!diagnosticControls_.empty())
        {
            diagnosticControl =
                std::move(diagnosticControls_.front());
            diagnosticControls_.pop_front();
        }
        else if (!packets_.empty())
        {
            packet = std::move(packets_.front());
            packets_.pop_front();
        }
    }
    const SOCKET socket = static_cast<SOCKET>(socketValue);
    if (!diagnosticControl.empty())
    {
        const std::string request =
            "{\"probeCommand\":\"" +
            diagnosticControl + "\"}";
        if (!WebSocketWire::SendClientText(socket, request))
        {
            RecordSendFailure();
            return false;
        }
        return true;
    }
    if (packet.empty()) return true;
    if (!WebSocketWire::SendClientBinary(socket, packet))
    {
        RecordSendFailure();
        return false;
    }
    RecordSent();
    return true;
}

void GameStreamClient::Impl::Run()
{
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0)
    {
        SetConnected(false, L"WSAStartup failed");
        return;
    }
    SOCKET socket = INVALID_SOCKET;
    while (!ShouldStop())
    {
        if (socket == INVALID_SOCKET)
        {
            std::string error;
            {
                std::lock_guard lock(mutex_);
                ++connectAttempts_;
            }
            socket = WebSocketWire::ConnectLocal(
                kGameStreamPort, error);
            if (socket == INVALID_SOCKET)
            {
                std::unique_lock lock(mutex_);
                lastError_ = Widen(error);
                wake_.wait_for(
                    lock, kRetryDelay, [this] { return stop_; });
                continue;
            }
            SetConnected(true, {});
        }
        if (!SendNext(static_cast<UINT_PTR>(socket)))
        {
            WebSocketWire::Close(socket);
            Notify(GameStreamEvent::StateChanged);
        }
        if (socket == INVALID_SOCKET) continue;

        WebSocketWire::Message message;
        const auto read = WebSocketWire::TryRead(
            socket, false, 0, message);
        if (read == WebSocketWire::ReadStatus::Message)
        {
            HandleControl(message.opcode, message.payload);
        }
        else if (read == WebSocketWire::ReadStatus::Closed ||
                 read == WebSocketWire::ReadStatus::Error)
        {
            WebSocketWire::Close(socket);
            SetConnected(false, L"websocket disconnected");
        }
    }
    WebSocketWire::Close(socket);
    WSACleanup();
}
