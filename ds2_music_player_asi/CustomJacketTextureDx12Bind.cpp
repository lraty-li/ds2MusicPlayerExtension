#include "pch.h"

#include "CustomJacketInternal.h"

#include "HookUtils.h"

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

std::string H(uint64_t value)
{
    return HookUtils::HexU64(value);
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
    oss << "txdx12bind " << phase
        << " tex=" << H(textureDx12)
        << " s88=" << H(s88)
        << " d90=" << H(d90)
        << " sD8=" << H(sD8)
        << " dE0=" << H(dE0);
    logger.Log(oss.str());
}

void ClearCopiedResourceState(uint64_t textureDx12)
{
    for (uint32_t offset : kResourceOffsets)
    {
        Write64(textureDx12 + offset, 0);
    }
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

bool ResolveBindFn(BindFn& bindFn, const Logger& logger)
{
    auto* gameModule = GetModuleHandleW(nullptr);
    const auto base = reinterpret_cast<uintptr_t>(gameModule);
    const auto bindAddr = base + kBindResourceHandleRva;
    if (!gameModule || !HookUtils::IsAddressRangeInModule(gameModule, bindAddr, 16))
    {
        logger.Log("txdx12bind skipped: bind address outside module");
        return false;
    }

    bindFn = reinterpret_cast<BindFn>(bindAddr);
    return true;
}
} // namespace

namespace CustomJacketInternal
{
bool TryBindTextureDx12ToSourceWrapper(uint64_t textureDx12,
    uint64_t sourceTextureDx12, const char* label, const Logger& logger)
{
    if (!textureDx12 || !sourceTextureDx12) return false;

    uint64_t wrapper = 0;
    uint64_t resource = 0;
    if (!Read64(sourceTextureDx12 + 0x88, wrapper) || !wrapper
        || !Read64(wrapper + 0x08, resource) || !resource)
    {
        logger.Log("txdx12bind skipped: source wrapper unreadable");
        return false;
    }

    BindFn bindFn = nullptr;
    if (!ResolveBindFn(bindFn, logger)) return false;

    HandleSlot slot = {};
    slot.q10 = wrapper;
    ResourceState backup = {};
    CaptureResourceState(textureDx12, backup);

    std::ostringstream begin;
    begin << "txdx12bind begin label=" << (label ? label : "")
        << " tex=" << H(textureDx12)
        << " source=" << H(sourceTextureDx12)
        << " wrapper=" << H(wrapper)
        << " resource=" << H(resource)
        << " slot=[" << H(slot.q0) << "," << H(slot.q8)
        << "," << H(slot.q10) << "]";
    logger.Log(begin.str());

    LogState("pre", textureDx12, logger);
    ClearCopiedResourceState(textureDx12);
    LogState("cleared", textureDx12, logger);

    if (!CallBind(bindFn, textureDx12, slot))
    {
        logger.Log("txdx12bind call failed");
        RestoreResourceState(textureDx12, backup);
        LogState("restored", textureDx12, logger);
        return false;
    }

    LogState("post", textureDx12, logger);
    return true;
}

bool TryBindTextureDx12CloneWrapperToSourceResource(uint64_t textureDx12,
    uint64_t sourceTextureDx12, const char* label, const Logger& logger)
{
    if (!textureDx12 || !sourceTextureDx12) return false;

    uint64_t cloneWrapper = 0;
    uint64_t oldResource = 0;
    uint64_t sourceWrapper = 0;
    uint64_t sourceResource = 0;
    if (!Read64(textureDx12 + 0x88, cloneWrapper) || !cloneWrapper
        || !Read64(cloneWrapper + 0x08, oldResource)
        || !Read64(sourceTextureDx12 + 0x88, sourceWrapper) || !sourceWrapper
        || !Read64(sourceWrapper + 0x08, sourceResource) || !sourceResource)
    {
        logger.Log("txdx12bind clonewrap skipped: wrapper/resource unreadable");
        return false;
    }

    BindFn bindFn = nullptr;
    if (!ResolveBindFn(bindFn, logger)) return false;

    ResourceState backup = {};
    CaptureResourceState(textureDx12, backup);
    HandleSlot slot = {};
    slot.q10 = cloneWrapper;

    std::ostringstream begin;
    begin << "txdx12bind clonewrap begin label=" << (label ? label : "")
        << " tex=" << H(textureDx12)
        << " cloneWrapper=" << H(cloneWrapper)
        << " oldResource=" << H(oldResource)
        << " source=" << H(sourceTextureDx12)
        << " sourceWrapper=" << H(sourceWrapper)
        << " sourceResource=" << H(sourceResource)
        << " slot=[" << H(slot.q0) << "," << H(slot.q8)
        << "," << H(slot.q10) << "]";
    logger.Log(begin.str());

    LogState("clonewrap pre", textureDx12, logger);
    Write64(cloneWrapper + 0x08, sourceResource);
    ClearCopiedResourceState(textureDx12);
    LogState("clonewrap cleared", textureDx12, logger);

    if (!CallBind(bindFn, textureDx12, slot))
    {
        logger.Log("txdx12bind clonewrap call failed");
        Write64(cloneWrapper + 0x08, oldResource);
        RestoreResourceState(textureDx12, backup);
        LogState("clonewrap restored", textureDx12, logger);
        return false;
    }

    uint64_t postWrapper = 0;
    uint64_t postResource = 0;
    Read64(textureDx12 + 0x88, postWrapper);
    if (postWrapper) Read64(postWrapper + 0x08, postResource);
    LogState("clonewrap post", textureDx12, logger);

    std::ostringstream end;
    end << "txdx12bind clonewrap result wrapper=" << H(postWrapper)
        << " resource=" << H(postResource)
        << " resourceEqSource=" << (postResource == sourceResource ? 1 : 0)
        << " wrapperEqClone=" << (postWrapper == cloneWrapper ? 1 : 0);
    logger.Log(end.str());
    return true;
}
} // namespace CustomJacketInternal
