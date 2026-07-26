#include "AudioSessionMute.h"

#include <Audioclient.h>
#include <Audiopolicy.h>
#include <Mmdeviceapi.h>
#include <TlHelp32.h>
#include <wrl.h>

#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
struct MutedSession
{
    ComPtr<ISimpleAudioVolume> volume;
    BOOL originalMute = FALSE;
};

using ParentMap = std::unordered_map<DWORD, DWORD>;

ParentMap SnapshotParentProcesses()
{
    ParentMap parents;
    const HANDLE snapshot =
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return parents;
    }

    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    if (Process32FirstW(snapshot, &process))
    {
        do
        {
            parents[process.th32ProcessID] =
                process.th32ParentProcessID;
        }
        while (Process32NextW(snapshot, &process));
    }
    CloseHandle(snapshot);
    return parents;
}

bool IsInProcessTree(
    DWORD processId, DWORD rootProcessId, const ParentMap& parents)
{
    for (size_t depth = 0; depth < 128 && processId != 0; ++depth)
    {
        if (processId == rootProcessId)
        {
            return true;
        }
        const auto parent = parents.find(processId);
        if (parent == parents.end() || parent->second == processId)
        {
            return false;
        }
        processId = parent->second;
    }
    return false;
}

HRESULT RestoreSessions(std::vector<MutedSession>& sessions)
{
    HRESULT firstFailure = S_OK;
    for (MutedSession& session : sessions)
    {
        const HRESULT result =
            session.volume->SetMute(session.originalMute, nullptr);
        if (FAILED(result) && SUCCEEDED(firstFailure))
        {
            firstFailure = result;
        }
    }
    sessions.clear();
    return firstFailure;
}

HRESULT MuteSessionsOnDevice(
    IMMDevice* device,
    DWORD rootProcessId,
    const ParentMap& parents,
    std::vector<MutedSession>& mutedSessions)
{
    ComPtr<IAudioSessionManager2> manager;
    HRESULT result = device->Activate(
        __uuidof(IAudioSessionManager2),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(manager.GetAddressOf()));
    if (FAILED(result))
    {
        return result;
    }

    ComPtr<IAudioSessionEnumerator> enumerator;
    result = manager->GetSessionEnumerator(&enumerator);
    if (FAILED(result))
    {
        return result;
    }

    int count = 0;
    result = enumerator->GetCount(&count);
    for (int index = 0; SUCCEEDED(result) && index < count; ++index)
    {
        ComPtr<IAudioSessionControl> control;
        result = enumerator->GetSession(index, &control);
        if (FAILED(result))
        {
            break;
        }

        ComPtr<IAudioSessionControl2> control2;
        DWORD processId = 0;
        if (FAILED(control.As(&control2)) ||
            control2->IsSystemSoundsSession() == S_OK ||
            FAILED(control2->GetProcessId(&processId)) ||
            !IsInProcessTree(processId, rootProcessId, parents))
        {
            continue;
        }

        ComPtr<ISimpleAudioVolume> volume;
        BOOL originalMute = FALSE;
        if (FAILED(control.As(&volume)) ||
            FAILED(volume->GetMute(&originalMute)))
        {
            continue;
        }
        result = volume->SetMute(TRUE, nullptr);
        if (FAILED(result))
        {
            break;
        }
        mutedSessions.push_back({ volume, originalMute });
    }
    return result;
}
}

class AudioSessionMuteController::Impl
{
public:
    std::vector<MutedSession> sessions;
};

AudioSessionMuteController::AudioSessionMuteController()
    : impl_(std::make_unique<Impl>())
{
}

AudioSessionMuteController::~AudioSessionMuteController()
{
    Restore();
}

AudioSessionMuteResult AudioSessionMuteController::Mute(
    DWORD rootProcessId)
{
    if (rootProcessId == 0)
    {
        return { E_INVALIDARG, 0 };
    }
    if (!impl_->sessions.empty())
    {
        return { S_FALSE, static_cast<UINT32>(impl_->sessions.size()) };
    }

    ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    HRESULT result = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&deviceEnumerator));
    if (FAILED(result))
    {
        return { result, 0 };
    }

    ComPtr<IMMDeviceCollection> devices;
    result = deviceEnumerator->EnumAudioEndpoints(
        eRender, DEVICE_STATE_ACTIVE, &devices);
    if (FAILED(result))
    {
        return { result, 0 };
    }

    const ParentMap parents = SnapshotParentProcesses();
    UINT deviceCount = 0;
    result = devices->GetCount(&deviceCount);
    for (UINT index = 0; SUCCEEDED(result) && index < deviceCount; ++index)
    {
        ComPtr<IMMDevice> device;
        result = devices->Item(index, &device);
        if (SUCCEEDED(result))
        {
            result = MuteSessionsOnDevice(
                device.Get(), rootProcessId, parents, impl_->sessions);
        }
    }
    if (FAILED(result))
    {
        RestoreSessions(impl_->sessions);
        return { result, 0 };
    }
    if (impl_->sessions.empty())
    {
        return { HRESULT_FROM_WIN32(ERROR_NOT_FOUND), 0 };
    }
    return {
        S_OK,
        static_cast<UINT32>(impl_->sessions.size())
    };
}

HRESULT AudioSessionMuteController::Restore()
{
    return RestoreSessions(impl_->sessions);
}

bool AudioSessionMuteController::IsMuted() const
{
    return !impl_->sessions.empty();
}
