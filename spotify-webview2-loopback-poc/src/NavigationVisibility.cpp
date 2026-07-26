#include "PocApp.h"

#include <string_view>
#include <wrl.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace
{
constexpr std::wstring_view kHelperOrigin =
    L"https://appassets.example/";
constexpr std::wstring_view kSpotifyAccountsOrigin =
    L"https://accounts.spotify.com/";
}

void PocApp::ConfigureDiagnosticEvents()
{
    if (!diagnosticsEnabled_)
    {
        return;
    }
    ComPtr<ICoreWebView2_8> audioView;
    if (FAILED(webView_.As(&audioView)))
    {
        return;
    }
    EventRegistrationToken ignoredToken{};
    audioView->add_IsDocumentPlayingAudioChanged(
        Callback<ICoreWebView2IsDocumentPlayingAudioChangedEventHandler>(
            [this](ICoreWebView2*, IUnknown*)
            {
                PostHostState();
                return S_OK;
            }).Get(),
        &ignoredToken);
    audioView->add_IsMutedChanged(
        Callback<ICoreWebView2IsMutedChangedEventHandler>(
            [this](ICoreWebView2*, IUnknown*)
            {
                PostHostState();
                return S_OK;
            }).Get(),
        &ignoredToken);
}

void PocApp::UpdateHelperWindowForNavigation()
{
    if (!helperMode_ || !webView_)
    {
        return;
    }
    LPWSTR rawSource = nullptr;
    if (FAILED(webView_->get_Source(&rawSource)) || !rawSource)
    {
        return;
    }
    const std::wstring_view source(rawSource);
    if (diagnosticsEnabled_)
    {
        SetHelperWindowVisible(true);
    }
    else if (source.starts_with(kSpotifyAccountsOrigin))
    {
        SetHelperWindowVisible(true);
    }
    else if (source.starts_with(kHelperOrigin))
    {
        SetHelperWindowVisible(false);
    }
    CoTaskMemFree(rawSource);
}
