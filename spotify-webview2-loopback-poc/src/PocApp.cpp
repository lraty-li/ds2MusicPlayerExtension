#include "PocApp.h"

#include <memory>
#include <sstream>

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
    gameStreamClient_.Start(window_);

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
    case kGameStreamEventMessage:
        HandleGameStreamEvent(
            static_cast<GameStreamEvent>(wParam));
        return 0;
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
    captureTargetProcessId_ =
        browserProcessId_ != 0 ? browserProcessId_ : GetCurrentProcessId();
    captureResult_ = capture_->Start(captureTargetProcessId_, window_);
    PostHostState();
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
    gameStreamClient_.Stop();
    sessionMuteController_.Restore();
    if (capture_)
    {
        capture_->Stop();
        capture_.Reset();
    }
    if (controller_)
    {
        controller_->Close();
    }
    pcmSharedRing_.Close();
    webView_.Reset();
    controller_.Reset();
    environment_.Reset();
}
