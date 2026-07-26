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
            gameStreamClient_.Push(ringChunk);
            if (diagnosticsEnabled_)
            {
                pcmStreamReceiver_.HandleChunk(
                    ringChunk, L"shared-ring", pcmMetrics);
            }
        }
        else if (diagnosticsEnabled_)
        {
            pcmStreamReceiver_.RecordInvalidChunk(
                L"shared-ring", ringError, pcmMetrics);
        }
        if (diagnosticsEnabled_ && !pcmMetrics.empty())
        {
            PostJson(pcmMetrics);
            PostGameStreamState();
        }
    }
    else if (diagnosticsEnabled_ &&
             pcmStreamReceiver_.HandleMessage(message, pcmMetrics))
    {
        if (!pcmMetrics.empty())
        {
            PostJson(pcmMetrics);
        }
    }
    else if (message == L"toggle-mute")
    {
        if (diagnosticsEnabled_) ToggleMute();
    }
    else if (message == L"toggle-session-mute")
    {
        if (diagnosticsEnabled_) ToggleSessionMute();
    }
    else if (message == L"request-host-state")
    {
        if (diagnosticsEnabled_) PostHostState();
    }
    else if (message == L"helper-auth-required")
    {
        AppendTelemetry("SESSION", L"helper_auth=required");
        if (diagnosticsEnabled_) SetHelperWindowVisible(true);
    }
    else if (message == L"helper-auth-ready")
    {
        AppendTelemetry("SESSION", L"helper_auth=ready");
        AppendStatus(L"authorization=ready");
        SetHelperWindowVisible(false);
    }
    else if (message.starts_with(L"helper-status:"))
    {
        AppendStatus(message.substr(14));
    }
    else if (message == L"helper-player-error")
    {
        AppendStatus(L"player=error");
    }
    else if (message == L"helper-diagnostics-ready")
    {
        SetHelperWindowVisible(true);
    }
    else if (message == L"helper-config-missing-client-id")
    {
        AppendStatus(L"config-client-id=missing");
        MessageBoxW(
            nullptr,
            L"Spotify Client ID is missing. Edit "
            L"scripts\\DS2SpotifyHelper\\config.json and restart the game.",
            L"Death Stranding 2 Spotify",
            MB_OK | MB_ICONWARNING);
    }
    else if (message == L"helper-auth-error")
    {
        AppendStatus(L"authorization=error");
        MessageBoxW(
            nullptr,
            L"Spotify authorization was not completed. "
            L"Restart the game to try again.",
            L"Death Stranding 2 Spotify",
            MB_OK | MB_ICONWARNING);
    }
    else if (message == L"helper-runtime-load-error" ||
             message == L"helper-runtime-error")
    {
        AppendStatus(message);
        MessageBoxW(
            nullptr,
            L"Spotify helper runtime failed to load.",
            L"Death Stranding 2 Spotify",
            MB_OK | MB_ICONERROR);
    }
    else if (message.starts_with(L"probe-control:"))
    {
        if (diagnosticsEnabled_)
        {
            gameStreamClient_.RequestProbeControl(message.substr(14));
            PostGameStreamState();
        }
    }
    else if (message.starts_with(L"game-json:"))
    {
        gameStreamClient_.PushText(message.substr(10));
        PostGameStreamState();
    }
    else if (message.starts_with(L"game-source-playing:"))
    {
        gameStreamClient_.SetSourcePlaying(message.ends_with(L"1"));
        PostGameStreamState();
    }
    else if (message == L"open-devtools" && webView_)
    {
        if (diagnosticsEnabled_) webView_->OpenDevToolsWindow();
    }
    else if (message.starts_with(L"web-log:"))
    {
        if (diagnosticsEnabled_)
        {
            AppendTelemetry("WEB", message.substr(8));
        }
    }
}
