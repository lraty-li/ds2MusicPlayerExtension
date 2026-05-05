#pragma once

#include <cstddef>
#include <cstdint>

using AkUInt8 = uint8_t;
using AkUInt16 = uint16_t;
using AkUInt32 = uint32_t;
using AkReal32 = float;
using AkPluginParamID = uint32_t;

enum AKRESULT
{
    AK_NotImplemented = 0,
    AK_Success = 1,
    AK_DataReady = 45
};

enum AkPluginType : AkUInt8
{
    AkPluginTypeSource = 2
};

constexpr AkUInt32 AK_FLOAT = 1;
constexpr AkUInt32 AK_NONINTERLEAVED = 1;
constexpr AkUInt32 AK_SPEAKER_SETUP_STEREO = 0x3;
constexpr AkUInt32 AK_ChannelConfigType_Standard = 0x1;

struct AkChannelConfig
{
    AkUInt32 uNumChannels : 8;
    AkUInt32 eConfigType : 4;
    AkUInt32 uChannelMask : 20;

    static AkChannelConfig Standard(AkUInt32 channelMask)
    {
        AkChannelConfig config = {};
        config.uNumChannels = channelMask == AK_SPEAKER_SETUP_STEREO ? 2 : 0;
        config.eConfigType = AK_ChannelConfigType_Standard;
        config.uChannelMask = channelMask;
        return config;
    }
};

struct AkAudioFormat
{
    AkUInt32 uSampleRate;
    AkChannelConfig channelConfig;
    AkUInt32 uBitsPerSample : 6;
    AkUInt32 uBlockAlign : 10;
    AkUInt32 uTypeID : 2;
    AkUInt32 uInterleaveID : 1;

    void SetAll(AkUInt32 sampleRate, AkChannelConfig config,
        AkUInt32 bits, AkUInt32 blockAlign, AkUInt32 type, AkUInt32 interleave)
    {
        uSampleRate = sampleRate;
        channelConfig = config;
        uBitsPerSample = bits;
        uBlockAlign = blockAlign;
        uTypeID = type;
        uInterleaveID = interleave;
    }
};

struct AkPluginInfo
{
    AkPluginInfo()
        : eType(static_cast<AkPluginType>(0)), uBuildVersion(0),
        bIsInPlace(true), bCanChangeRate(false), bReserved(false),
        bCanProcessObjects(false), bIsDeviceEffect(false),
        bCanRunOnObjectConfig(true), bUsesGainAttribute(false)
    {
    }

    AkPluginType eType;
    AkUInt32 uBuildVersion;
    bool bIsInPlace;
    bool bCanChangeRate;
    bool bReserved;
    bool bCanProcessObjects;
    bool bIsDeviceEffect;
    bool bCanRunOnObjectConfig;
    bool bUsesGainAttribute;
};

class AkAudioBuffer
{
public:
    AkUInt32 NumChannels() const { return channelConfig.uNumChannels; }
    bool HasData() const { return pData != nullptr; }
    float* GetChannel(AkUInt32 index)
    {
        return reinterpret_cast<float*>(
            static_cast<AkUInt8*>(pData) + index * sizeof(float) * MaxFrames());
    }
    AkUInt16 MaxFrames() const { return uMaxFrames; }

protected:
    void* pData;
    AkChannelConfig channelConfig;

public:
    AKRESULT eState;

protected:
    AkUInt16 uMaxFrames;

public:
    AkUInt16 uValidFrames;
};

namespace AK
{
class PluginRegistration;
class IAkPluginParam;
class IAkSourcePluginContext;

class IAkPluginMemAlloc
{
protected:
    virtual ~IAkPluginMemAlloc() {}

public:
    virtual void* Malloc(size_t size, const char* file, AkUInt32 line) = 0;
    virtual void Free(void* memory) = 0;
    virtual void* Malign(size_t size, size_t alignment,
        const char* file, AkUInt32 line) = 0;
    virtual void* Realloc(void* memory, size_t size,
        const char* file, AkUInt32 line) = 0;
    virtual void* ReallocAligned(void* memory, size_t size, size_t alignment,
        const char* file, AkUInt32 line) = 0;
};

class IAkRTPCSubscriber
{
protected:
    virtual ~IAkRTPCSubscriber() {}

public:
    virtual AKRESULT SetParam(AkPluginParamID id,
        const void* value, AkUInt32 size) = 0;
};

class IAkPluginParam : public IAkRTPCSubscriber
{
protected:
    virtual ~IAkPluginParam() {}

public:
    virtual IAkPluginParam* Clone(IAkPluginMemAlloc* allocator) = 0;
    virtual AKRESULT Init(IAkPluginMemAlloc* allocator,
        const void* paramsBlock, AkUInt32 blockSize) = 0;
    virtual AKRESULT Term(IAkPluginMemAlloc* allocator) = 0;
    virtual AKRESULT SetParamsBlock(const void* paramsBlock, AkUInt32 size) = 0;
};

class IAkPlugin
{
protected:
    virtual ~IAkPlugin() {}

public:
    virtual AKRESULT Term(IAkPluginMemAlloc* allocator) = 0;
    virtual AKRESULT Reset() = 0;
    virtual AKRESULT GetPluginInfo(AkPluginInfo& info) = 0;
    virtual bool SupportMediaRelocation() const { return false; }
    virtual AKRESULT RelocateMedia(AkUInt8*, AkUInt8*) { return AK_NotImplemented; }
};

class IAkSourcePlugin : public IAkPlugin
{
protected:
    virtual ~IAkSourcePlugin() {}

public:
    virtual AKRESULT Init(IAkPluginMemAlloc* allocator,
        IAkSourcePluginContext* context, IAkPluginParam* params,
        AkAudioFormat& format) = 0;
    virtual AkReal32 GetDuration() const = 0;
    virtual AkReal32 GetEnvelope() const { return 1.0f; }
    virtual AKRESULT StopLooping() { return AK_Success; }
    virtual AKRESULT Seek(AkUInt32) { return AK_Success; }
    virtual AKRESULT TimeSkip(AkUInt32&) { return AK_NotImplemented; }
    virtual void Execute(AkAudioBuffer* buffer) = 0;
};
}
