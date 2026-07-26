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
    if (message == L"toggle-mute")
    {
        ToggleMute();
    }
    else if (message == L"request-host-state")
    {
        PostHostState();
    }
    else if (message == L"open-devtools" && webView_)
    {
        webView_->OpenDevToolsWindow();
    }
}
