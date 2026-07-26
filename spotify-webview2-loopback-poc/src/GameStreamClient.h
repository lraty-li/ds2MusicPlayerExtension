#pragma once

#include "PcmChunkCodec.h"

#include <Windows.h>

#include <memory>
#include <string>
#include <string_view>

inline constexpr UINT kGameStreamEventMessage = WM_APP + 0x241;

enum class GameStreamEvent : WPARAM
{
    StateChanged = 1,
    Pause = 2,
    Resume = 3,
};

class GameStreamClient
{
public:
    GameStreamClient();
    ~GameStreamClient();

    GameStreamClient(const GameStreamClient&) = delete;
    GameStreamClient& operator=(const GameStreamClient&) = delete;

    void Start(HWND notifyWindow);
    void Stop();
    void Push(const DecodedPcmChunk& chunk);
    void PushText(std::wstring_view json);
    void SetSourcePlaying(bool playing);
    void RequestProbeControl(std::wstring_view command);
    std::wstring MetricsJson() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
