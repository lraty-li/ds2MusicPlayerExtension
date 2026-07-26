#pragma once

#include "CaptureMetrics.h"
#include "ProcessLoopbackCapture.h"
#include "WebView2.h"

#include <Windows.h>
#include <wrl.h>

#include <string>

class PocApp
{
public:
    PocApp() = default;
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
    void ConfigureWebView();
    void ConfigureAutoplay();
    void StartCapture();
    void ResizeWebView();
    void ToggleMute();
    void ExecuteDiagnosticScript(const wchar_t* script);
    void HandleWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args);
    void PostHostState();
    void PostMetrics(const CaptureMetrics& metrics);
    void PostJson(const std::wstring& json);
    void ShowFailure(const wchar_t* stage, HRESULT result);
    void Shutdown();

    HWND window_ = nullptr;
    bool shuttingDown_ = false;
    bool webContentReady_ = false;
    HRESULT captureResult_ = E_PENDING;
    UINT32 browserProcessId_ = 0;
    std::wstring configuredClientId_;
    std::wstring runtimeVersion_;
    std::wstring webFolder_;
    std::wstring userDataFolder_;

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webView_;
    Microsoft::WRL::ComPtr<ProcessLoopbackCapture> capture_;
};
