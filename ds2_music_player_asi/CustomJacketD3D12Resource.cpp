#include "pch.h"

#include "CustomJacketInternal.h"

#include "HookUtils.h"

#include <d3d12.h>
#include <sstream>

namespace
{
constexpr uintptr_t kBindResourceHandleRva = 0x2116B40;

using BindFn = void(__fastcall*)(uint64_t textureDx12, void* handleSlot);

struct HandleSlot
{
    uint64_t q0 = 0x304;
    uint64_t q8 = 0;
    uint64_t q10 = 0;
};

struct ResourceState
{
    uint64_t q[18] = {};
};

const uint32_t kResourceOffsets[] = {
    0x78, 0x80, 0x88, 0x90,
    0x98, 0xA0, 0xA8, 0xB0, 0xB8,
    0xC8, 0xD0, 0xD8, 0xE0, 0xE8,
    0xF0, 0xF8, 0x100, 0x108
};

std::string H(uint64_t value)
{
    return HookUtils::HexU64(value);
}

bool Read64(uint64_t addr, uint64_t& out)
{
    __try
    {
        out = *reinterpret_cast<uint64_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out = 0;
        return false;
    }
}

bool Write64(uint64_t addr, uint64_t value)
{
    __try
    {
        *reinterpret_cast<uint64_t*>(addr) = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ResolveBindFn(BindFn& bindFn, const Logger& logger)
{
    auto* gameModule = GetModuleHandleW(nullptr);
    const auto base = reinterpret_cast<uintptr_t>(gameModule);
    const auto bindAddr = base + kBindResourceHandleRva;
    if (!gameModule || !HookUtils::IsAddressRangeInModule(gameModule, bindAddr, 16))
    {
        logger.Log("txdx12own skipped: bind address outside module");
        return false;
    }
    bindFn = reinterpret_cast<BindFn>(bindAddr);
    return true;
}

void CaptureResourceState(uint64_t textureDx12, ResourceState& out)
{
    for (uint32_t i = 0; i < ARRAYSIZE(kResourceOffsets); ++i)
    {
        Read64(textureDx12 + kResourceOffsets[i], out.q[i]);
    }
}

void RestoreResourceState(uint64_t textureDx12, const ResourceState& state)
{
    for (uint32_t i = 0; i < ARRAYSIZE(kResourceOffsets); ++i)
    {
        Write64(textureDx12 + kResourceOffsets[i], state.q[i]);
    }
}

void ClearCopiedResourceState(uint64_t textureDx12)
{
    for (uint32_t offset : kResourceOffsets)
    {
        Write64(textureDx12 + offset, 0);
    }
}

void LogState(const char* phase, uint64_t textureDx12, const Logger& logger)
{
    uint64_t s88 = 0;
    uint64_t d90 = 0;
    uint64_t sD8 = 0;
    uint64_t dE0 = 0;
    Read64(textureDx12 + 0x88, s88);
    Read64(textureDx12 + 0x90, d90);
    Read64(textureDx12 + 0xD8, sD8);
    Read64(textureDx12 + 0xE0, dE0);

    std::ostringstream oss;
    oss << "txdx12own " << phase
        << " tex=" << H(textureDx12)
        << " s88=" << H(s88)
        << " d90=" << H(d90)
        << " sD8=" << H(sD8)
        << " dE0=" << H(dE0);
    logger.Log(oss.str());
}

bool CallBind(BindFn bindFn, uint64_t textureDx12, HandleSlot& slot)
{
    __try
    {
        bindFn(textureDx12, &slot);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

} // namespace

namespace CustomJacketInternal
{
bool TryBindTextureDx12CloneWrapperToNewResource(uint64_t textureDx12,
    uint64_t sourceTextureDx12, const char* label, const Logger& logger)
{
    uint64_t cloneWrapper = 0;
    uint64_t oldResource = 0;
    uint64_t sourceWrapper = 0;
    uint64_t sourceResource = 0;
    if (!Read64(textureDx12 + 0x88, cloneWrapper) || !cloneWrapper
        || !Read64(cloneWrapper + 0x08, oldResource)
        || !Read64(sourceTextureDx12 + 0x88, sourceWrapper) || !sourceWrapper
        || !Read64(sourceWrapper + 0x08, sourceResource) || !sourceResource)
    {
        logger.Log("txdx12own skipped: wrapper/resource unreadable");
        return false;
    }

    uint64_t ownResource = 0;
    if (!TryCreateCustomJacketD3D12ResourceLike(sourceResource, ownResource, logger))
    {
        return false;
    }

    BindFn bindFn = nullptr;
    if (!ResolveBindFn(bindFn, logger))
    {
        reinterpret_cast<ID3D12Resource*>(ownResource)->Release();
        return false;
    }

    ResourceState backup = {};
    CaptureResourceState(textureDx12, backup);
    HandleSlot slot = {};
    slot.q10 = cloneWrapper;

    std::ostringstream begin;
    begin << "txdx12own begin label=" << (label ? label : "")
        << " tex=" << H(textureDx12)
        << " cloneWrapper=" << H(cloneWrapper)
        << " oldResource=" << H(oldResource)
        << " ownResource=" << H(ownResource)
        << " sourceResource=" << H(sourceResource);
    logger.Log(begin.str());

    TryUploadCustomJacketD3D12TestPattern(ownResource, logger);
    Write64(cloneWrapper + 0x08, ownResource);
    LogState("pre", textureDx12, logger);
    ClearCopiedResourceState(textureDx12);
    LogState("cleared", textureDx12, logger);

    if (!CallBind(bindFn, textureDx12, slot))
    {
        logger.Log("txdx12own bind failed");
        Write64(cloneWrapper + 0x08, oldResource);
        RestoreResourceState(textureDx12, backup);
        reinterpret_cast<ID3D12Resource*>(ownResource)->Release();
        LogState("restored", textureDx12, logger);
        return false;
    }

    uint64_t postWrapper = 0;
    uint64_t postResource = 0;
    Read64(textureDx12 + 0x88, postWrapper);
    if (postWrapper) Read64(postWrapper + 0x08, postResource);
    LogState("post", textureDx12, logger);

    std::ostringstream end;
    end << "txdx12own result wrapper=" << H(postWrapper)
        << " resource=" << H(postResource)
        << " resourceEqOwn=" << (postResource == ownResource ? 1 : 0)
        << " wrapperEqClone=" << (postWrapper == cloneWrapper ? 1 : 0);
    logger.Log(end.str());
    return true;
}
} // namespace CustomJacketInternal
