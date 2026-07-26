#pragma once

#include "GameStreamClient.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

class GameStreamClient::Impl
{
public:
    void Start(HWND notifyWindow);
    void Stop();
    void Push(const DecodedPcmChunk& chunk);
    void RequestProbeControl(std::wstring_view command);
    std::wstring MetricsJson() const;

private:
    bool ShouldStop() const;
    void Notify(GameStreamEvent event) const;
    void SetConnected(bool connected, std::wstring error);
    void RecordSent();
    void RecordSendFailure();
    void HandleControl(uint8_t opcode, const std::vector<uint8_t>& payload);
    bool SendNext(UINT_PTR socketValue);
    void Run();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    HWND notifyWindow_ = nullptr;
    bool started_ = false;
    bool stop_ = false;
    bool connected_ = false;
    std::deque<std::vector<uint8_t>> packets_;
    std::deque<std::string> diagnosticControls_;
    std::vector<uint8_t> pendingPcm_;
    std::wstring sourceStreamId_;
    std::wstring lastError_;
    uint64_t nextSequence_ = 0;
    uint64_t connectAttempts_ = 0;
    uint64_t connections_ = 0;
    uint64_t packetsSent_ = 0;
    uint64_t framesSent_ = 0;
    uint64_t pcmBytesSent_ = 0;
    uint64_t maxQueueDepth_ = 0;
    uint64_t droppedPackets_ = 0;
    uint64_t sendErrors_ = 0;
    uint64_t invalidChunks_ = 0;
    uint64_t pauseCommands_ = 0;
    uint64_t resumeCommands_ = 0;
};
