#pragma once

#include "CaptureMetrics.h"

#include <AudioClient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl.h>
#include <wrl/implements.h>

#include <atomic>
#include <cstdint>
#include <thread>

class ProcessLoopbackCapture final :
    public Microsoft::WRL::RuntimeClass<
        Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
        Microsoft::WRL::FtmBase,
        IActivateAudioInterfaceCompletionHandler>
{
public:
    ProcessLoopbackCapture() = default;
    ~ProcessLoopbackCapture();

    HRESULT Start(DWORD targetProcessId, HWND notifyWindow);
    void Stop();
    bool IsRunning() const;

    STDMETHOD(ActivateCompleted)(
        IActivateAudioInterfaceAsyncOperation* operation) override;

private:
    HRESULT BeginActivation(DWORD targetProcessId);
    HRESULT ConfigureAudioClient();
    void CaptureLoop();
    HRESULT DrainPackets();
    void PublishMetrics(bool force);
    void PublishError(HRESULT error);
    void CloseEvents();

    Microsoft::WRL::ComPtr<IActivateAudioInterfaceAsyncOperation> activationOperation_;
    Microsoft::WRL::ComPtr<IAudioClient> audioClient_;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient_;
    std::thread captureThread_;
    std::atomic<bool> running_{false};
    HWND notifyWindow_ = nullptr;
    HANDLE activationEvent_ = nullptr;
    HANDLE sampleEvent_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    HRESULT activationResult_ = E_UNEXPECTED;
    WAVEFORMATEX format_{};

    uint64_t sampleCount_ = 0;
    uint64_t nonzeroSamples_ = 0;
    uint64_t totalFrames_ = 0;
    uint64_t qpcPosition_ = 0;
    double squareSum_ = 0.0;
    int peakAbsolute_ = 0;
    uint32_t silentPackets_ = 0;
    uint32_t discontinuities_ = 0;
    uint32_t timestampErrors_ = 0;
    ULONGLONG lastPublishTick_ = 0;
};
