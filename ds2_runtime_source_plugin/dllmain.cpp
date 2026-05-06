#include "pch.h"

#include "AudioStreamServer.h"
#include "PluginLog.h"
#include "PluginParams.h"
#include "WwisePluginAbi.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <new>

namespace
{
constexpr uint32_t kCompanyId = 0x6A7;
constexpr uint32_t kPluginId = 0x101;
constexpr uint32_t kWwiseBuildVersion = 517633;
constexpr uint32_t kSampleRate = 48000;
constexpr float kToneHz = 440.0f;
constexpr float kToneGain = 0.18f;
constexpr float kPi = 3.14159265358979323846f;

void Log(const char* text)
{
    PluginLog::Write(text);
}

class Ds2SourcePlugin final : public AK::IAkSourcePlugin
{
public:
    AKRESULT Term(AK::IAkPluginMemAlloc* allocator) override
    {
        Log("plugin Term");
        this->~Ds2SourcePlugin();
        allocator->Free(this);
        return AK_Success;
    }

    AKRESULT Reset() override
    {
        phase_ = 0.0f;
        executeCalls_ = 0;
        Log("plugin Reset");
        return AK_Success;
    }

    AKRESULT GetPluginInfo(AkPluginInfo& info) override
    {
        info = AkPluginInfo();
        info.eType = AkPluginTypeSource;
        info.uBuildVersion = kWwiseBuildVersion;
        info.bIsInPlace = true;
        Log("plugin GetPluginInfo source build=517633");
        return AK_Success;
    }

    AKRESULT Init(AK::IAkPluginMemAlloc*, AK::IAkSourcePluginContext*,
        AK::IAkPluginParam*, AkAudioFormat& format) override
    {
        AkChannelConfig config = AkChannelConfig::Standard(AK_SPEAKER_SETUP_STEREO);
        format.SetAll(kSampleRate, config, 32, 8, AK_FLOAT, AK_NONINTERLEAVED);
        Reset();
        Log("plugin Init format=48000 stereo float");
        return AK_Success;
    }

    AkReal32 GetDuration() const override
    {
        return 0.0f;
    }

    AkReal32 GetEnvelope() const override
    {
        return 1.0f;
    }

    AKRESULT StopLooping() override
    {
        Log("plugin StopLooping");
        return AK_Success;
    }

    AKRESULT Seek(AkUInt32) override
    {
        Log("plugin Seek");
        return AK_Success;
    }

    AKRESULT TimeSkip(AkUInt32& frames) override
    {
        const float inc = 2.0f * kPi * kToneHz / static_cast<float>(kSampleRate);
        phase_ += inc * static_cast<float>(frames);
        while (phase_ >= 2.0f * kPi)
        {
            phase_ -= 2.0f * kPi;
        }
        Log("plugin TimeSkip");
        return AK_DataReady;
    }

    void Execute(AkAudioBuffer* buffer) override
    {
        if (!buffer || !buffer->HasData() || buffer->MaxFrames() == 0)
        {
            return;
        }

        const AkUInt16 frames = buffer->MaxFrames();
        const AkUInt32 channels = buffer->NumChannels() ?
            buffer->NumChannels() : 2;
        float* channel0 = buffer->GetChannel(0);
        const uint32_t copied = AudioStreamServer::Read(channel0, frames, channels);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            float* out = buffer->GetChannel(ch);
            for (AkUInt16 i = static_cast<AkUInt16>(copied); i < frames; ++i) {
                out[i] = 0.0f;
            }
        }

        buffer->uValidFrames = frames;
        buffer->eState = AK_DataReady;
        ++executeCalls_;
        if (executeCalls_ == 1 || (executeCalls_ % 200) == 0)
        {
            char msg[128] = {};
            sprintf_s(msg, "plugin Execute calls=%u frames=%u channels=%u streamFrames=%u",
                executeCalls_, frames, channels, copied);
            Log(msg);
        }
    }

    static AK::IAkSourcePlugin* Create(AK::IAkPluginMemAlloc* allocator)
    {
        void* memory = allocator->Malloc(sizeof(Ds2SourcePlugin),
            __FILE__, __LINE__);
        if (!memory)
        {
            Log("createPlugin allocation failed");
            return nullptr;
        }
        return new (memory) Ds2SourcePlugin();
    }

private:
    float phase_ = 0.0f;
    uint32_t executeCalls_ = 0;
};

}

AK::IAkPlugin* CreateDS2MusicResource(AK::IAkPluginMemAlloc* allocator)
{
    Log("createPlugin");
    return Ds2SourcePlugin::Create(allocator);
}

struct PluginListItem
{
    PluginListItem* next;
    uint8_t type;
    uint8_t reserved09[3];
    uint32_t companyId;
    uint32_t pluginId;
    uint32_t reserved14;
    void* createPlugin;
    void* createParams;
    void* reserved28;
    void* reserved30;
    void* registeredCallback;
    void* registeredCallbackUser;
    void* thirdCallback;
    void* codecField;
};

static_assert(offsetof(PluginListItem, type) == 0x08);
static_assert(offsetof(PluginListItem, companyId) == 0x0C);
static_assert(offsetof(PluginListItem, pluginId) == 0x10);
static_assert(offsetof(PluginListItem, createPlugin) == 0x18);
static_assert(offsetof(PluginListItem, createParams) == 0x20);
static_assert(offsetof(PluginListItem, thirdCallback) == 0x48);
static_assert(offsetof(PluginListItem, codecField) == 0x50);

PluginListItem g_pluginEntry = {
    nullptr,
    AkPluginTypeSource,
    {},
    kCompanyId,
    kPluginId,
    0,
    reinterpret_cast<void*>(&CreateDS2MusicResource),
    reinterpret_cast<void*>(&CreateDS2MusicResourceParams),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

extern "C" __declspec(dllexport) AK::PluginRegistration* g_pAKPluginList =
    reinterpret_cast<AK::PluginRegistration*>(&g_pluginEntry);

extern "C" __declspec(dllexport) int DS2AudioStreamSendBrowserControl(const char* json)
{
    return AudioStreamServer::SendControl(json) ? 1 : 0;
}

extern "C" __declspec(dllexport) int DS2AudioStreamReadMetadataTitle(
    char* output, unsigned int outputBytes)
{
    return AudioStreamServer::ReadMetadataTitle(output, outputBytes);
}

extern "C" __declspec(dllexport) int DS2AudioStreamReadMetadata(
    char* title, unsigned int titleBytes, char* artist, unsigned int artistBytes)
{
    return AudioStreamServer::ReadMetadata(title, titleBytes, artist, artistBytes);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        PluginLog::Reset();
        Log("DLL_PROCESS_ATTACH");
        AudioStreamServer::Start();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        AudioStreamServer::Stop();
        Log("DLL_PROCESS_DETACH");
    }

    return TRUE;
}
