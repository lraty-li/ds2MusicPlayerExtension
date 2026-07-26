#include "PocApp.h"

#include <iomanip>
#include <sstream>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

void PocApp::PostHostState()
{
    BOOL muted = FALSE;
    BOOL playing = FALSE;
    ComPtr<ICoreWebView2_8> audioView;
    if (webView_ && SUCCEEDED(webView_.As(&audioView)))
    {
        audioView->get_IsMuted(&muted);
        audioView->get_IsDocumentPlayingAudio(&playing);
    }

    std::wostringstream json;
    json << L"{\"type\":\"host-state\""
         << L",\"helperPid\":" << GetCurrentProcessId()
         << L",\"browserPid\":" << browserProcessId_
         << L",\"captureTargetPid\":" << captureTargetProcessId_
         << L",\"runtime\":\"" << runtimeVersion_ << L"\""
         << L",\"proxyServer\":\""
         << (configuredProxyServer_.empty()
                ? L"系统代理"
                : configuredProxyServer_)
         << L"\""
         << L",\"muted\":" << (muted ? L"true" : L"false")
         << L",\"sessionMuted\":"
         << (sessionMuteController_.IsMuted() ? L"true" : L"false")
         << L",\"sessionMuteCount\":" << sessionMuteCount_
         << L",\"sessionMuteResult\":"
         << static_cast<uint32_t>(sessionMuteResult_)
         << L",\"documentPlayingAudio\":"
         << (playing ? L"true" : L"false")
         << L",\"captureActive\":"
         << (capture_ && capture_->IsRunning() ? L"true" : L"false")
         << L",\"captureResult\":"
         << static_cast<uint32_t>(captureResult_)
         << L",\"sharedRingReady\":"
         << (pcmSharedRing_.IsReady() ? L"true" : L"false")
         << L",\"sharedRingResult\":"
         << static_cast<uint32_t>(sharedRingResult_)
         << L",\"sharedRingPostResult\":"
         << static_cast<uint32_t>(sharedRingPostResult_)
         << L",\"sharedRingPostCount\":" << sharedRingPostCount_
         << L"}";
    PostJson(json.str());
}

void PocApp::PostMetrics(const CaptureMetrics& metrics)
{
    std::wostringstream json;
    json << std::fixed << std::setprecision(7)
         << L"{\"type\":\"capture-metrics\""
         << L",\"active\":" << (metrics.active ? L"true" : L"false")
         << L",\"error\":" << static_cast<uint32_t>(metrics.error)
         << L",\"sampleRate\":48000"
         << L",\"channels\":2"
         << L",\"rms\":" << metrics.rms
         << L",\"peak\":" << metrics.peak
         << L",\"nonzeroRatio\":" << metrics.nonzeroRatio
         << L",\"totalFrames\":" << metrics.totalFrames
         << L",\"qpcPosition\":" << metrics.qpcPosition
         << L",\"silentPackets\":" << metrics.silentPackets
         << L",\"discontinuities\":" << metrics.discontinuities
         << L",\"timestampErrors\":" << metrics.timestampErrors
         << L"}";
    PostJson(json.str());
}

void PocApp::PostJson(const std::wstring& json)
{
    AppendTelemetry("HOST", json);
    if (webView_ && webContentReady_)
    {
        webView_->PostWebMessageAsJson(json.c_str());
    }
}
