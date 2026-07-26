#pragma once

#include "AudioSessionMute.h"
#include "CaptureMetrics.h"
#include "GameStreamClient.h"
#include "PcmSharedRing.h"
#include "PcmStreamReceiver.h"
#include "ProcessLoopbackCapture.h"
#include "WebView2.h"

#include <Windows.h>
#include <wrl.h>

#include <string>

class PocApp
{
public:
    explicit PocApp(bool helperMode = false)
        : helperMode_(helperMode)
    {
    }
    ~PocApp();

    int Run(HINSTANCE instance, int showCommand);

private:
    static LRESULT CALLBACK WindowProcedure(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleWindowMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool CreateMainWindow(HINSTANCE instance, int showCommand);
    void InitializeWebView();
    HRESULT OnEnvironmentCreated(
        HRESULT result, ICoreWebView2Environment* environment);
    HRESULT OnControllerCreated(
        HRESULT result, ICoreWebView2Controller* controller);
    HRESULT InstallFrameAudioProbeAndNavigate();
    void ConfigureWebView();
    void ConfigureAutoplay();
    void InitializeSharedPcmRing(
        ICoreWebView2Environment* environment);
    void ConfigureSharedPcmFrames();
    void WatchSharedPcmFrame(ICoreWebView2Frame* frame);
    void PostSharedPcmRingToFrame(ICoreWebView2Frame2* frame);
    void StartCapture();
    void ResizeWebView();
    void ToggleMute();
    void ToggleSessionMute();
    void SetHelperWindowVisible(bool visible);
    void ExecuteDiagnosticScript(const wchar_t* script);
    void HandleWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args);
    void HandleGameStreamEvent(GameStreamEvent event);
    void PostHostState();
    void PostMetrics(const CaptureMetrics& metrics);
    void PostGameStreamState();
    void PostJson(const std::wstring& json);
    void ResetTelemetry();
    void AppendTelemetry(
        const char* source, const std::wstring& payload);
    void ShowFailure(const wchar_t* stage, HRESULT result);
    void Shutdown();

    HWND window_ = nullptr;
    bool helperMode_ = false;
    bool shuttingDown_ = false;
    bool webContentReady_ = false;
    HRESULT captureResult_ = E_PENDING;
    HRESULT sharedRingResult_ = E_PENDING;
    HRESULT sharedRingPostResult_ = E_PENDING;
    HRESULT sessionMuteResult_ = S_OK;
    UINT32 browserProcessId_ = 0;
    UINT32 captureTargetProcessId_ = 0;
    UINT32 sessionMuteCount_ = 0;
    UINT32 sharedRingPostCount_ = 0;
    std::wstring configuredClientId_;
    std::wstring configuredProxyServer_;
    std::wstring runtimeVersion_;
    std::wstring webFolder_;
    std::wstring userDataFolder_;

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webView_;
    Microsoft::WRL::ComPtr<ProcessLoopbackCapture> capture_;
    AudioSessionMuteController sessionMuteController_;
    GameStreamClient gameStreamClient_;
    PcmSharedRing pcmSharedRing_;
    PcmStreamReceiver pcmStreamReceiver_;
};
