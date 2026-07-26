#pragma once

#include <Windows.h>

#include <memory>

struct AudioSessionMuteResult
{
    HRESULT result = E_FAIL;
    UINT32 sessionCount = 0;
};

class AudioSessionMuteController
{
public:
    AudioSessionMuteController();
    ~AudioSessionMuteController();

    AudioSessionMuteController(const AudioSessionMuteController&) = delete;
    AudioSessionMuteController& operator=(
        const AudioSessionMuteController&) = delete;

    AudioSessionMuteResult Mute(DWORD rootProcessId);
    HRESULT Restore();
    bool IsMuted() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
