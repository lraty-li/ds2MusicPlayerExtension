#include "PocApp.h"

#include <sstream>
#include <wrl.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace
{
constexpr wchar_t kSpotifySdkOriginJson[] =
    L"\"https://sdk.scdn.co\"";
}

void PocApp::InitializeSharedPcmRing(
    ICoreWebView2Environment* environment)
{
    sharedRingResult_ = pcmSharedRing_.Initialize(environment);
    std::wostringstream entry;
    entry << L"pcm_shared_ring=create result="
          << static_cast<uint32_t>(sharedRingResult_)
          << L" ready="
          << (pcmSharedRing_.IsReady() ? L"true" : L"false");
    AppendTelemetry("SESSION", entry.str());
}

void PocApp::ConfigureSharedPcmFrames()
{
    if (!webView_ || !pcmSharedRing_.IsReady())
    {
        return;
    }
    ComPtr<ICoreWebView2_4> frameSource;
    sharedRingPostResult_ = webView_.As(&frameSource);
    if (FAILED(sharedRingPostResult_))
    {
        return;
    }
    EventRegistrationToken ignoredToken{};
    sharedRingPostResult_ = frameSource->add_FrameCreated(
        Callback<ICoreWebView2FrameCreatedEventHandler>(
            [this](
                ICoreWebView2*,
                ICoreWebView2FrameCreatedEventArgs* args)
            {
                if (shuttingDown_ || !args)
                {
                    return S_OK;
                }
                ComPtr<ICoreWebView2Frame> frame;
                if (SUCCEEDED(args->get_Frame(&frame)) && frame)
                {
                    WatchSharedPcmFrame(frame.Get());
                }
                return S_OK;
            }).Get(),
        &ignoredToken);
}

void PocApp::WatchSharedPcmFrame(ICoreWebView2Frame* frame)
{
    ComPtr<ICoreWebView2Frame2> navigableFrame;
    if (!frame || FAILED(frame->QueryInterface(
            IID_PPV_ARGS(&navigableFrame))))
    {
        return;
    }
    EventRegistrationToken ignoredToken{};
    navigableFrame->add_NavigationCompleted(
        Callback<ICoreWebView2FrameNavigationCompletedEventHandler>(
            [this](
                ICoreWebView2Frame* sender,
                ICoreWebView2NavigationCompletedEventArgs* args)
            {
                BOOL success = FALSE;
                if (shuttingDown_ || !sender || !args ||
                    FAILED(args->get_IsSuccess(&success)) || !success)
                {
                    return S_OK;
                }
                ComPtr<ICoreWebView2Frame2> scriptFrame;
                if (FAILED(sender->QueryInterface(
                        IID_PPV_ARGS(&scriptFrame))))
                {
                    return S_OK;
                }
                const ComPtr<ICoreWebView2Frame2> retainedFrame =
                    scriptFrame;
                scriptFrame->ExecuteScript(
                    L"location.origin",
                    Callback<
                        ICoreWebView2ExecuteScriptCompletedHandler>(
                        [this, retainedFrame](
                            HRESULT result,
                            LPCWSTR originJson)
                        {
                            if (!shuttingDown_ &&
                                SUCCEEDED(result) &&
                                originJson &&
                                std::wstring_view(originJson) ==
                                    kSpotifySdkOriginJson)
                            {
                                PostSharedPcmRingToFrame(
                                    retainedFrame.Get());
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get(),
        &ignoredToken);
}

void PocApp::PostSharedPcmRingToFrame(
    ICoreWebView2Frame2* frame)
{
    ComPtr<ICoreWebView2Frame4> sharedFrame;
    if (!frame || !pcmSharedRing_.IsReady() ||
        FAILED(frame->QueryInterface(IID_PPV_ARGS(&sharedFrame))))
    {
        return;
    }
    sharedRingPostResult_ = sharedFrame->PostSharedBufferToScript(
        pcmSharedRing_.Buffer(),
        COREWEBVIEW2_SHARED_BUFFER_ACCESS_READ_WRITE,
        pcmSharedRing_.DescriptorJson().c_str());
    if (SUCCEEDED(sharedRingPostResult_))
    {
        ++sharedRingPostCount_;
    }
    std::wostringstream entry;
    entry << L"pcm_shared_ring=post-sdk-frame result="
          << static_cast<uint32_t>(sharedRingPostResult_)
          << L" count=" << sharedRingPostCount_;
    AppendTelemetry("SESSION", entry.str());
    PostHostState();
}
