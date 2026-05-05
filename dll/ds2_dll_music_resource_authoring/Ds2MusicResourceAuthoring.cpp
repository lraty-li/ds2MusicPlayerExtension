#include <AK/Wwise/Plugin.h>

namespace
{
constexpr AkUInt32 kCompanyId = 1703;
constexpr AkUInt32 kPluginId = 257;

class Ds2StubParams final : public AK::IAkPluginParam
{
public:
    AK::IAkPluginParam* Clone(AK::IAkPluginMemAlloc*) override
    {
        return new Ds2StubParams();
    }

    AKRESULT Init(
        AK::IAkPluginMemAlloc*,
        const void*,
        AkUInt32) override
    {
        return AK_Success;
    }

    AKRESULT Term(AK::IAkPluginMemAlloc*) override
    {
        delete this;
        return AK_Success;
    }

    AKRESULT SetParamsBlock(const void*, AkUInt32) override
    {
        return AK_Success;
    }

    AKRESULT SetParam(AkPluginParamID, const void*, AkUInt32) override
    {
        return AK_Success;
    }
};

class Ds2StubSource final : public AK::IAkSourcePlugin
{
public:
    AKRESULT Init(
        AK::IAkPluginMemAlloc*,
        AK::IAkSourcePluginContext*,
        AK::IAkPluginParam*,
        AkAudioFormat&) override
    {
        return AK_Success;
    }

    AKRESULT Term(AK::IAkPluginMemAlloc*) override
    {
        delete this;
        return AK_Success;
    }

    AKRESULT Reset() override
    {
        return AK_Success;
    }

    AKRESULT GetPluginInfo(AkPluginInfo& outInfo) override
    {
        outInfo.eType = AkPluginTypeSource;
        outInfo.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;
        return AK_Success;
    }

    AkReal32 GetDuration() const override
    {
        return -1.0f;
    }

    void Execute(AkAudioBuffer* ioBuffer) override
    {
        if (ioBuffer)
        {
            ioBuffer->uValidFrames = 0;
            ioBuffer->eState = AK_NoMoreData;
        }
    }
};

AK::IAkPlugin* CreateDs2MusicResourceSource(AK::IAkPluginMemAlloc*)
{
    return new Ds2StubSource();
}

AK::IAkPluginParam* CreateDs2MusicResourceSourceParams(AK::IAkPluginMemAlloc*)
{
    return new Ds2StubParams();
}

AK_ATTR_USED AK::PluginRegistration Ds2MusicResourceSourceRegistration(
    AkPluginTypeSource,
    kCompanyId,
    kPluginId,
    CreateDs2MusicResourceSource,
    CreateDs2MusicResourceSourceParams);

class Ds2MusicResourceAuthoring final
    : public AK::Wwise::Plugin::AudioPlugin
    , public AK::Wwise::Plugin::Source
{
public:
    bool GetBankParameters(
        const GUID&,
        AK::Wwise::Plugin::DataWriter&) const override
    {
        return true;
    }

    bool GetSourceDuration(
        double& outMinDuration,
        double& outMaxDuration) const override
    {
        outMinDuration = 0.0;
        outMaxDuration = 0.0;
        return false;
    }
};
}

AK_DEFINE_PLUGIN_CONTAINER(ds2_dll_music_resource);
AK_EXPORT_PLUGIN_CONTAINER(ds2_dll_music_resource);
AK_ADD_PLUGIN_CLASS_TO_CONTAINER(
    ds2_dll_music_resource,
    Ds2MusicResourceAuthoring,
    Ds2MusicResourceSource);

DEFINE_PLUGIN_REGISTER_HOOK;

#ifdef DEFINE_PLUGIN_ASSERT_HOOK
DEFINE_PLUGIN_ASSERT_HOOK;
#else
DEFINEDUMMYASSERTHOOK;
#endif
