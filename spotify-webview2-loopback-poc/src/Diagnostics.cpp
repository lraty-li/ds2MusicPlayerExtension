#include "PocApp.h"

#include <wrl.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

void PocApp::ExecuteDiagnosticScript(const wchar_t* script)
{
    if (!webView_ || !script)
    {
        return;
    }
    webView_->ExecuteScript(
        script,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [](HRESULT, LPCWSTR)
            {
                return S_OK;
            }).Get());
}

void PocApp::ToggleMute()
{
    ComPtr<ICoreWebView2_8> audioView;
    if (!webView_ || FAILED(webView_.As(&audioView)))
    {
        return;
    }
    BOOL muted = FALSE;
    if (SUCCEEDED(audioView->get_IsMuted(&muted)))
    {
        audioView->put_IsMuted(!muted);
        PostHostState();
    }
}

void PocApp::ToggleSessionMute()
{
    if (sessionMuteController_.IsMuted())
    {
        sessionMuteResult_ = sessionMuteController_.Restore();
        sessionMuteCount_ = 0;
    }
    else
    {
        const AudioSessionMuteResult outcome =
            sessionMuteController_.Mute(browserProcessId_);
        sessionMuteResult_ = outcome.result;
        sessionMuteCount_ = outcome.sessionCount;
    }
    PostHostState();
}

void PocApp::HandleWebMessage(
    ICoreWebView2WebMessageReceivedEventArgs* args)
{
    LPWSTR rawMessage = nullptr;
    if (FAILED(args->TryGetWebMessageAsString(&rawMessage)) || !rawMessage)
    {
        return;
    }
    const std::wstring message(rawMessage);
    CoTaskMemFree(rawMessage);
    std::wstring pcmMetrics;
    DecodedPcmChunk ringChunk;
    std::wstring ringError;
    if (pcmSharedRing_.HandleCommit(message, ringChunk, ringError))
    {
        if (ringError.empty())
        {
            pcmStreamReceiver_.HandleChunk(
                ringChunk, L"shared-ring", pcmMetrics);
        }
        else
        {
            pcmStreamReceiver_.RecordInvalidChunk(
                L"shared-ring", ringError, pcmMetrics);
        }
        if (!pcmMetrics.empty())
        {
            PostJson(pcmMetrics);
        }
    }
    else if (pcmStreamReceiver_.HandleMessage(message, pcmMetrics))
    {
        if (!pcmMetrics.empty())
        {
            PostJson(pcmMetrics);
        }
    }
    else if (message == L"toggle-mute")
    {
        ToggleMute();
    }
    else if (message == L"toggle-session-mute")
    {
        ToggleSessionMute();
    }
    else if (message == L"request-host-state")
    {
        PostHostState();
    }
    else if (message == L"open-devtools" && webView_)
    {
        webView_->OpenDevToolsWindow();
    }
    else if (message.starts_with(L"web-log:"))
    {
        AppendTelemetry("WEB", message.substr(8));
    }
}
