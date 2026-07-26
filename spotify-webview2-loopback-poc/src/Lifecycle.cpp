#include "PocApp.h"

#include <wrl.h>

using Microsoft::WRL::Callback;

bool PocApp::StartGameProcessWatch()
{
    if (gameProcessId_ == 0)
    {
        return true;
    }
    gameProcess_ = OpenProcess(SYNCHRONIZE, FALSE, gameProcessId_);
    if (!gameProcess_)
    {
        return false;
    }
    if (!RegisterWaitForSingleObject(
            &gameProcessWait_,
            gameProcess_,
            OnGameProcessExited,
            this,
            INFINITE,
            WT_EXECUTEONLYONCE))
    {
        CloseHandle(gameProcess_);
        gameProcess_ = nullptr;
        return false;
    }
    return true;
}

void PocApp::StopGameProcessWatch()
{
    if (gameProcessWait_)
    {
        UnregisterWaitEx(gameProcessWait_, INVALID_HANDLE_VALUE);
        gameProcessWait_ = nullptr;
    }
    if (gameProcess_)
    {
        CloseHandle(gameProcess_);
        gameProcess_ = nullptr;
    }
}

void CALLBACK PocApp::OnGameProcessExited(
    void* context, BOOLEAN)
{
    auto* app = static_cast<PocApp*>(context);
    if (app && app->window_)
    {
        PostMessageW(
            app->window_,
            kGameProcessExitedMessage,
            0,
            0);
    }
}

void PocApp::BeginShutdown()
{
    if (shutdownRequested_ || !window_)
    {
        return;
    }
    shutdownRequested_ = true;
    if (!webView_ || !webContentReady_)
    {
        DestroyWindow(window_);
        return;
    }

    webView_->ExecuteScript(
        L"window.__pocSpotifyShutdown && "
        L"window.__pocSpotifyShutdown()",
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [](HRESULT, LPCWSTR)
            {
                return S_OK;
            }).Get());
    if (SetTimer(window_, kShutdownTimerId, 500, nullptr) == 0)
    {
        DestroyWindow(window_);
    }
}
