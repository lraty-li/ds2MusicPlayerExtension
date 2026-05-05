#include "pch.h"

#include "PluginParams.h"

#include "PluginLog.h"

#include <new>
#include <cstdio>

namespace
{
void Log(const char* text);

class Ds2PluginParams final : public AK::IAkPluginParam
{
public:
    Ds2PluginParams() = default;

    AK::IAkPluginParam* Clone(AK::IAkPluginMemAlloc* allocator) override
    {
        Log("params Clone");
        return Create(allocator);
    }

    AKRESULT Init(AK::IAkPluginMemAlloc*, const void*, AkUInt32 size) override
    {
        char msg[96] = {};
        sprintf_s(msg, "params Init blockSize=%u", size);
        Log(msg);
        return AK_Success;
    }

    AKRESULT Term(AK::IAkPluginMemAlloc* allocator) override
    {
        Log("params Term");
        this->~Ds2PluginParams();
        allocator->Free(this);
        return AK_Success;
    }

    AKRESULT SetParamsBlock(const void*, AkUInt32 size) override
    {
        char msg[96] = {};
        sprintf_s(msg, "params SetParamsBlock size=%u", size);
        Log(msg);
        return AK_Success;
    }

    AKRESULT SetParam(AkPluginParamID id, const void*, AkUInt32 size) override
    {
        char msg[96] = {};
        sprintf_s(msg, "params SetParam id=%u size=%u", id, size);
        Log(msg);
        return AK_Success;
    }

    static AK::IAkPluginParam* Create(AK::IAkPluginMemAlloc* allocator)
    {
        void* memory = allocator->Malloc(sizeof(Ds2PluginParams),
            __FILE__, __LINE__);
        if (!memory)
        {
            Log("createParams allocation failed");
            return nullptr;
        }
        return new (memory) Ds2PluginParams();
    }
};

void Log(const char* text)
{
    PluginLog::Write(text);
}
} // namespace

AK::IAkPluginParam* CreateDS2MusicResourceParams(
    AK::IAkPluginMemAlloc* allocator)
{
    Log("createParams");
    return Ds2PluginParams::Create(allocator);
}
