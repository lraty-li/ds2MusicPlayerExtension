#include "ProcessLoopbackCapture.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <propidl.h>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr DWORD kActivationTimeoutMs = 10000;
constexpr DWORD kPublishIntervalMs = 500;
constexpr WORD kChannels = 2;
constexpr DWORD kSampleRate = 48000;
constexpr WORD kBitsPerSample = 16;
}

ProcessLoopbackCapture::~ProcessLoopbackCapture()
{
    Stop();
    CloseEvents();
}

HRESULT ProcessLoopbackCapture::Start(DWORD targetProcessId, HWND notifyWindow)
{
    if (running_.load() || !notifyWindow || targetProcessId == 0)
    {
        return E_INVALIDARG;
    }

    notifyWindow_ = notifyWindow;
    activationEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    sampleEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!activationEvent_ || !sampleEvent_ || !stopEvent_)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HRESULT result = BeginActivation(targetProcessId);
    if (FAILED(result))
    {
        return result;
    }
    if (WaitForSingleObject(activationEvent_, kActivationTimeoutMs) != WAIT_OBJECT_0)
    {
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (FAILED(activationResult_))
    {
        return activationResult_;
    }

    result = ConfigureAudioClient();
    if (FAILED(result))
    {
        return result;
    }

    running_.store(true);
    lastPublishTick_ = GetTickCount64();
    captureThread_ = std::thread(&ProcessLoopbackCapture::CaptureLoop, this);
    result = audioClient_->Start();
    if (FAILED(result))
    {
        running_.store(false);
        SetEvent(stopEvent_);
        captureThread_.join();
        return result;
    }
    return S_OK;
}

void ProcessLoopbackCapture::Stop()
{
    const bool wasRunning = running_.exchange(false);
    if (stopEvent_)
    {
        SetEvent(stopEvent_);
    }
    if (captureThread_.joinable())
    {
        captureThread_.join();
    }
    if (wasRunning && audioClient_)
    {
        audioClient_->Stop();
    }
    captureClient_.Reset();
    audioClient_.Reset();
    activationOperation_.Reset();
}

bool ProcessLoopbackCapture::IsRunning() const
{
    return running_.load();
}

HRESULT ProcessLoopbackCapture::BeginActivation(DWORD targetProcessId)
{
    AUDIOCLIENT_ACTIVATION_PARAMS parameters{};
    parameters.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    parameters.ProcessLoopbackParams.TargetProcessId = targetProcessId;
    parameters.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT variant{};
    variant.vt = VT_BLOB;
    variant.blob.cbSize = sizeof(parameters);
    variant.blob.pBlobData = reinterpret_cast<BYTE*>(&parameters);

    return ActivateAudioInterfaceAsync(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient),
        &variant,
        this,
        &activationOperation_);
}

HRESULT ProcessLoopbackCapture::ActivateCompleted(
    IActivateAudioInterfaceAsyncOperation* operation)
{
    ComPtr<IUnknown> activated;
    HRESULT operationResult = E_UNEXPECTED;
    HRESULT result = operation->GetActivateResult(&operationResult, &activated);
    activationResult_ = FAILED(result) ? result : operationResult;
    if (SUCCEEDED(activationResult_))
    {
        activationResult_ = activated.As(&audioClient_);
    }
    SetEvent(activationEvent_);
    return S_OK;
}

HRESULT ProcessLoopbackCapture::ConfigureAudioClient()
{
    format_.wFormatTag = WAVE_FORMAT_PCM;
    format_.nChannels = kChannels;
    format_.nSamplesPerSec = kSampleRate;
    format_.wBitsPerSample = kBitsPerSample;
    format_.nBlockAlign = kChannels * kBitsPerSample / 8;
    format_.nAvgBytesPerSec = kSampleRate * format_.nBlockAlign;

    const DWORD flags =
        AUDCLNT_STREAMFLAGS_LOOPBACK |
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    HRESULT result = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &format_, nullptr);
    if (FAILED(result))
    {
        return result;
    }
    result = audioClient_->GetService(IID_PPV_ARGS(&captureClient_));
    if (FAILED(result))
    {
        return result;
    }
    return audioClient_->SetEventHandle(sampleEvent_);
}

void ProcessLoopbackCapture::CaptureLoop()
{
    const HANDLE events[] = { stopEvent_, sampleEvent_ };
    while (running_.load())
    {
        const DWORD wait = WaitForMultipleObjects(2, events, FALSE, kPublishIntervalMs);
        if (wait == WAIT_OBJECT_0)
        {
            break;
        }
        if (wait == WAIT_OBJECT_0 + 1)
        {
            const HRESULT result = DrainPackets();
            if (FAILED(result))
            {
                PublishError(result);
                running_.store(false);
                break;
            }
        }
        PublishMetrics(wait == WAIT_TIMEOUT);
    }
    PublishMetrics(true);
}

HRESULT ProcessLoopbackCapture::DrainPackets()
{
    UINT32 availableFrames = 0;
    HRESULT result = captureClient_->GetNextPacketSize(&availableFrames);
    while (SUCCEEDED(result) && availableFrames > 0)
    {
        BYTE* bytes = nullptr;
        DWORD flags = 0;
        UINT64 devicePosition = 0;
        UINT64 qpcPosition = 0;
        result = captureClient_->GetBuffer(
            &bytes, &availableFrames, &flags, &devicePosition, &qpcPosition);
        if (FAILED(result))
        {
            return result;
        }

        const uint64_t samples = static_cast<uint64_t>(availableFrames) * kChannels;
        sampleCount_ += samples;
        totalFrames_ += availableFrames;
        qpcPosition_ = qpcPosition;
        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
        {
            ++silentPackets_;
        }
        else
        {
            const auto* pcm = reinterpret_cast<const int16_t*>(bytes);
            for (uint64_t index = 0; index < samples; ++index)
            {
                const int sampleValue = static_cast<int>(pcm[index]);
                const int absolute = sampleValue < 0 ? -sampleValue : sampleValue;
                peakAbsolute_ = std::max(peakAbsolute_, absolute);
                nonzeroSamples_ += sampleValue != 0 ? 1 : 0;
                const double normalized =
                    static_cast<double>(sampleValue) / 32768.0;
                squareSum_ += normalized * normalized;
            }
        }
        discontinuities_ +=
            (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0 ? 1 : 0;
        timestampErrors_ +=
            (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0 ? 1 : 0;
        captureClient_->ReleaseBuffer(availableFrames);
        result = captureClient_->GetNextPacketSize(&availableFrames);
    }
    return result;
}

void ProcessLoopbackCapture::PublishMetrics(bool force)
{
    const ULONGLONG now = GetTickCount64();
    if (!force && now - lastPublishTick_ < kPublishIntervalMs)
    {
        return;
    }
    lastPublishTick_ = now;

    auto* metrics = new (std::nothrow) CaptureMetrics();
    if (!metrics)
    {
        return;
    }
    metrics->active = running_.load();
    metrics->rms = sampleCount_ == 0 ? 0.0 :
        std::sqrt(squareSum_ / static_cast<double>(sampleCount_));
    metrics->peak = static_cast<double>(peakAbsolute_) / 32768.0;
    metrics->nonzeroRatio = sampleCount_ == 0 ? 0.0 :
        static_cast<double>(nonzeroSamples_) / static_cast<double>(sampleCount_);
    metrics->totalFrames = totalFrames_;
    metrics->qpcPosition = qpcPosition_;
    metrics->silentPackets = silentPackets_;
    metrics->discontinuities = discontinuities_;
    metrics->timestampErrors = timestampErrors_;
    if (!PostMessageW(notifyWindow_, kCaptureMetricsMessage, 0,
        reinterpret_cast<LPARAM>(metrics)))
    {
        delete metrics;
    }
    sampleCount_ = 0;
    nonzeroSamples_ = 0;
    squareSum_ = 0.0;
    peakAbsolute_ = 0;
}

void ProcessLoopbackCapture::PublishError(HRESULT error)
{
    auto* metrics = new (std::nothrow) CaptureMetrics();
    if (!metrics)
    {
        return;
    }
    metrics->error = error;
    metrics->active = false;
    if (!PostMessageW(notifyWindow_, kCaptureMetricsMessage, 0,
        reinterpret_cast<LPARAM>(metrics)))
    {
        delete metrics;
    }
}

void ProcessLoopbackCapture::CloseEvents()
{
    for (HANDLE* handle : { &activationEvent_, &sampleEvent_, &stopEvent_ })
    {
        if (*handle)
        {
            CloseHandle(*handle);
            *handle = nullptr;
        }
    }
}
