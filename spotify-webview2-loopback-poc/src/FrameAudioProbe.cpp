#include "PocApp.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <wrl.h>

using Microsoft::WRL::Callback;

namespace
{
constexpr wchar_t kStartUrl[] = L"https://appassets.example/index.html";
constexpr wchar_t kProbeScript[] = L"frame-audio-probe-injected.js";
constexpr wchar_t kPcmBridgeScript[] = L"frame-pcm-bridge-injected.js";
constexpr wchar_t kGraphScript[] = L"frame-media-graph-injected.js";

HRESULT LoadUtf8File(
    const std::filesystem::path& path,
    std::wstring& output)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    std::string bytes{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    if (bytes.starts_with("\xEF\xBB\xBF"))
    {
        bytes.erase(0, 3);
    }
    if (bytes.empty())
    {
        return E_INVALIDARG;
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        bytes.data(),
        static_cast<int>(bytes.size()),
        nullptr,
        0);
    if (required <= 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    output.resize(static_cast<size_t>(required));
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            bytes.data(),
            static_cast<int>(bytes.size()),
            output.data(),
            required) != required)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}
}

HRESULT PocApp::InstallFrameAudioProbeAndNavigate()
{
    if (!webView_)
    {
        return E_UNEXPECTED;
    }

    std::wstring probeScript;
    std::wstring pcmBridgeScript;
    std::wstring graphScript;
    const std::filesystem::path webFolder(webFolder_);
    HRESULT result = LoadUtf8File(webFolder / kProbeScript, probeScript);
    if (SUCCEEDED(result))
    {
        result = LoadUtf8File(
            webFolder / kPcmBridgeScript, pcmBridgeScript);
    }
    if (SUCCEEDED(result))
    {
        result = LoadUtf8File(webFolder / kGraphScript, graphScript);
    }
    if (FAILED(result))
    {
        ShowFailure(L"Load frame audio probe scripts", result);
        return result;
    }
    probeScript += L"\n";
    probeScript += pcmBridgeScript;
    probeScript += L"\n";
    probeScript += graphScript;

    const HRESULT installResult =
        webView_->AddScriptToExecuteOnDocumentCreated(
            probeScript.c_str(),
            Callback<
                ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                [this](HRESULT callbackResult, LPCWSTR)
                {
                    if (shuttingDown_)
                    {
                        return S_OK;
                    }
                    if (FAILED(callbackResult))
                    {
                        ShowFailure(
                            L"Install frame audio probe",
                            callbackResult);
                        return callbackResult;
                    }
                    AppendTelemetry(
                        "SESSION",
                        L"frame_audio_probe=installed");
                    std::wstring startUrl = kStartUrl;
                    if (!configuredClientId_.empty())
                    {
                        startUrl += L"?client_id=";
                        startUrl += configuredClientId_;
                    }
                    const HRESULT navigateResult =
                        webView_->Navigate(startUrl.c_str());
                    if (FAILED(navigateResult))
                    {
                        ShowFailure(
                            L"Navigate start page",
                            navigateResult);
                    }
                    return S_OK;
                }).Get());
    if (FAILED(installResult))
    {
        ShowFailure(L"Queue frame audio probe", installResult);
    }
    return installResult;
}
