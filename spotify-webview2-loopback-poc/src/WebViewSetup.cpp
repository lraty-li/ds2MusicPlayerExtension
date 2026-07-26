#include "PocApp.h"
#include "PocConfig.h"
#include "WebView2EnvironmentOptions.h"

#include <KnownFolders.h>
#include <ShlObj.h>
#include <filesystem>
#include <wrl.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace
{
constexpr wchar_t kVirtualHost[] = L"appassets.example";
constexpr wchar_t kHostOrigin[] = L"https://appassets.example";
constexpr wchar_t kSdkOrigin[] = L"https://sdk.scdn.co";

std::wstring ExecutableFolder()
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(
        std::wstring(path, length)).parent_path().wstring();
}

std::wstring PersistentUserDataFolder()
{
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(
        FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData)))
    {
        return {};
    }
    std::filesystem::path folder(localAppData);
    CoTaskMemFree(localAppData);
    folder /= L"DS2SpotifyWebView2LoopbackPoc";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    return error ? std::wstring() : folder.wstring();
}
}

void PocApp::InitializeWebView()
{
    const std::filesystem::path executableFolder = ExecutableFolder();
    webFolder_ = (executableFolder / L"web").wstring();
    configuredClientId_ =
        LoadSpotifyClientId(executableFolder / L"config.json");
    configuredProxyServer_ =
        LoadProxyServer(executableFolder / L"config.json");
    userDataFolder_ = PersistentUserDataFolder();
    if (!std::filesystem::exists(
        std::filesystem::path(webFolder_) / L"index.html"))
    {
        ShowFailure(L"Locate web/index.html", HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        return;
    }
    if (userDataFolder_.empty())
    {
        ShowFailure(L"Create WebView2 user data folder", E_FAIL);
        return;
    }
    ResetTelemetry();
    AppendTelemetry(
        "SESSION",
        configuredClientId_.empty()
            ? L"config_client_id=missing"
            : L"config_client_id=loaded");
    AppendTelemetry(
        "SESSION",
        configuredProxyServer_.empty()
            ? L"proxy_server=system"
            : L"proxy_server=" + configuredProxyServer_);
    if (helperMode_)
    {
        AppendTelemetry(
            "SESSION", L"autoplay_policy=no-user-gesture-required");
    }

    LPWSTR version = nullptr;
    const HRESULT versionResult =
        GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
    if (SUCCEEDED(versionResult) && version)
    {
        runtimeVersion_ = version;
        CoTaskMemFree(version);
    }
    else
    {
        ShowFailure(L"Find WebView2 Evergreen Runtime", versionResult);
        return;
    }

    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    if (!options)
    {
        ShowFailure(L"Create WebView2 environment options", E_OUTOFMEMORY);
        return;
    }
    std::wstring browserArguments = helperMode_
        ? L"--autoplay-policy=no-user-gesture-required"
        : L"";
    if (!configuredProxyServer_.empty())
    {
        if (!browserArguments.empty()) browserArguments += L" ";
        browserArguments += L"--proxy-server=" + configuredProxyServer_;
    }
    if (!browserArguments.empty())
    {
        const HRESULT optionResult =
            options->put_AdditionalBrowserArguments(
                browserArguments.c_str());
        if (FAILED(optionResult))
        {
            ShowFailure(L"Configure WebView2 proxy", optionResult);
            return;
        }
    }

    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userDataFolder_.c_str(),
        options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT error, ICoreWebView2Environment* environment)
            {
                return OnEnvironmentCreated(error, environment);
            }).Get());
    if (FAILED(result))
    {
        ShowFailure(L"Create WebView2 environment", result);
    }
}

HRESULT PocApp::OnEnvironmentCreated(
    HRESULT result, ICoreWebView2Environment* environment)
{
    if (shuttingDown_)
    {
        return S_OK;
    }
    if (FAILED(result) || !environment)
    {
        ShowFailure(L"Create WebView2 environment", result);
        return result;
    }
    environment_ = environment;
    InitializeSharedPcmRing(environment);
    return environment_->CreateCoreWebView2Controller(
        window_,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this](HRESULT error, ICoreWebView2Controller* controller)
            {
                return OnControllerCreated(error, controller);
            }).Get());
}

HRESULT PocApp::OnControllerCreated(
    HRESULT result, ICoreWebView2Controller* controller)
{
    if (shuttingDown_)
    {
        return S_OK;
    }
    if (FAILED(result) || !controller)
    {
        ShowFailure(L"Create WebView2 controller", result);
        return result;
    }
    controller_ = controller;
    result = controller_->get_CoreWebView2(&webView_);
    if (FAILED(result))
    {
        ShowFailure(L"Get CoreWebView2", result);
        return result;
    }
    webView_->get_BrowserProcessId(&browserProcessId_);
    ConfigureWebView();
    ConfigureAutoplay();
    ResizeWebView();
    if (!helperMode_) StartCapture();
    return InstallFrameAudioProbeAndNavigate();
}

void PocApp::ConfigureWebView()
{
    EventRegistrationToken ignoredToken{};
    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(webView_->get_Settings(&settings)))
    {
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDevToolsEnabled(helperMode_ ? FALSE : TRUE);
    }
    ConfigureSharedPcmFrames();

    ComPtr<ICoreWebView2_3> localContent;
    if (SUCCEEDED(webView_.As(&localContent)))
    {
        localContent->SetVirtualHostNameToFolderMapping(
            kVirtualHost,
            webFolder_.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    }

    webView_->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*,
                ICoreWebView2WebMessageReceivedEventArgs* args)
            {
                HandleWebMessage(args);
                return S_OK;
            }).Get(),
        &ignoredToken);

    webView_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2*,
                ICoreWebView2NavigationCompletedEventArgs* args)
            {
                BOOL success = FALSE;
                args->get_IsSuccess(&success);
                webContentReady_ = success != FALSE;
                PostHostState();
                PostGameStreamState();
                return S_OK;
            }).Get(),
        &ignoredToken);

    webView_->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [](ICoreWebView2*,
                ICoreWebView2PermissionRequestedEventArgs* args)
            {
                COREWEBVIEW2_PERMISSION_KIND kind{};
                args->get_PermissionKind(&kind);
                if (kind == COREWEBVIEW2_PERMISSION_KIND_AUTOPLAY)
                {
                    args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
                }
                return S_OK;
            }).Get(),
        &ignoredToken);

    ComPtr<ICoreWebView2_8> audioView;
    if (SUCCEEDED(webView_.As(&audioView)))
    {
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
}

void PocApp::ConfigureAutoplay()
{
    ComPtr<ICoreWebView2_13> profileSource;
    ComPtr<ICoreWebView2Profile> profile;
    ComPtr<ICoreWebView2Profile4> permissionProfile;
    if (FAILED(webView_.As(&profileSource)) ||
        FAILED(profileSource->get_Profile(&profile)) ||
        FAILED(profile.As(&permissionProfile)))
    {
        return;
    }

    auto completed =
        Callback<ICoreWebView2SetPermissionStateCompletedHandler>(
            [this](HRESULT)
            {
                PostHostState();
                return S_OK;
            });
    permissionProfile->SetPermissionState(
        COREWEBVIEW2_PERMISSION_KIND_AUTOPLAY,
        kHostOrigin,
        COREWEBVIEW2_PERMISSION_STATE_ALLOW,
        completed.Get());
    permissionProfile->SetPermissionState(
        COREWEBVIEW2_PERMISSION_KIND_AUTOPLAY,
        kSdkOrigin,
        COREWEBVIEW2_PERMISSION_STATE_ALLOW,
        completed.Get());
}
