#include "PocApp.h"

#include <iomanip>
#include <memory>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr wchar_t kWindowClass[] = L"DS2SpotifyWebView2LoopbackPoc";
constexpr wchar_t kWindowTitle[] =
    L"Spotify Connect WebView2 + Process Loopback PoC";
}

PocApp::~PocApp()
{
    Shutdown();
}

int PocApp::Run(HINSTANCE instance, int showCommand)
{
    if (!CreateMainWindow(instance, showCommand))
    {
        return static_cast<int>(GetLastError());
    }
    InitializeWebView();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool PocApp::CreateMainWindow(HINSTANCE instance, int showCommand)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&windowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        kWindowClass,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1120,
        840,
        nullptr,
        nullptr,
        instance,
        this);
    if (!window_)
    {
        return false;
    }
    ShowWindow(window_, showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand);
    UpdateWindow(window_);
    return true;
}

LRESULT CALLBACK PocApp::WindowProcedure(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    PocApp* app = reinterpret_cast<PocApp*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<PocApp*>(create->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(app));
    }
    return app
        ? app->HandleWindowMessage(message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT PocApp::HandleWindowMessage(
    UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        ResizeWebView();
        return 0;
    case kCaptureMetricsMessage:
    {
        std::unique_ptr<CaptureMetrics> metrics(
            reinterpret_cast<CaptureMetrics*>(lParam));
        if (metrics)
        {
            PostMetrics(*metrics);
        }
        return 0;
    }
    case kDiagnosticToneMessage:
        ExecuteDiagnosticScript(
            L"window.__pocToggleTone && window.__pocToggleTone()");
        return 0;
    case kDiagnosticMuteMessage:
        ToggleMute();
        return 0;
    case kDiagnosticMetricsMessage:
        ExecuteDiagnosticScript(
            L"document.getElementById('capture-verdict').scrollIntoView()");
        return 0;
    case kDiagnosticLogMessage:
        ExecuteDiagnosticScript(
            L"document.getElementById('log').scrollIntoView()");
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_F8)
        {
            ExecuteDiagnosticScript(
                L"window.__pocToggleTone && window.__pocToggleTone()");
            return 0;
        }
        if (wParam == VK_F9)
        {
            ToggleMute();
            return 0;
        }
        if (wParam == VK_F10)
        {
            ExecuteDiagnosticScript(
                L"document.getElementById('capture-verdict').scrollIntoView()");
            return 0;
        }
        if (wParam == VK_F11)
        {
            ExecuteDiagnosticScript(
                L"document.getElementById('log').scrollIntoView()");
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        Shutdown();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
    }
}

void PocApp::ResizeWebView()
{
    if (!controller_ || !window_)
    {
        return;
    }
    RECT bounds{};
    GetClientRect(window_, &bounds);
    controller_->put_Bounds(bounds);
}

void PocApp::StartCapture()
{
    capture_ = Microsoft::WRL::Make<ProcessLoopbackCapture>();
    if (!capture_)
    {
        captureResult_ = E_OUTOFMEMORY;
        PostHostState();
        return;
    }
    captureResult_ = capture_->Start(GetCurrentProcessId(), window_);
    PostHostState();
}

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
         << L",\"runtime\":\"" << runtimeVersion_ << L"\""
         << L",\"muted\":" << (muted ? L"true" : L"false")
         << L",\"documentPlayingAudio\":"
         << (playing ? L"true" : L"false")
         << L",\"captureActive\":"
         << (capture_ && capture_->IsRunning() ? L"true" : L"false")
         << L",\"captureResult\":"
         << static_cast<uint32_t>(captureResult_)
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
    if (webView_ && webContentReady_)
    {
        webView_->PostWebMessageAsJson(json.c_str());
    }
}

void PocApp::ShowFailure(const wchar_t* stage, HRESULT result)
{
    std::wostringstream message;
    message << stage << L" failed: 0x"
            << std::hex << static_cast<uint32_t>(result);
    MessageBoxW(window_, message.str().c_str(), kWindowTitle,
        MB_OK | MB_ICONERROR);
}

void PocApp::Shutdown()
{
    if (shuttingDown_)
    {
        return;
    }
    shuttingDown_ = true;
    webContentReady_ = false;
    if (capture_)
    {
        capture_->Stop();
        capture_.Reset();
    }
    if (controller_)
    {
        controller_->Close();
    }
    webView_.Reset();
    controller_.Reset();
    environment_.Reset();
}
