#include "pch.h"

#include "CustomJacketInternal.h"

#include "SpecialTrackHelpers.h"

namespace CustomJacketInternal
{
bool SehReadU64(uint64_t addr, uint64_t& out)
{
    __try
    {
        out = *reinterpret_cast<uint64_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SehMemcpySafe(void* dst, const void* src, size_t size)
{
    __try
    {
        memcpy(dst, src, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SehWritePtrVal(uint8_t* base, int32_t offset, void* val)
{
    __try
    {
        *reinterpret_cast<void**>(base + offset) = val;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SehReadSlot(uint64_t slotAddr, CustomJacketSlot& out)
{
    __try
    {
        auto* slot = reinterpret_cast<uint64_t*>(slotAddr);
        if (!slot || !slot[0]) return false;
        out.target = slot[0];
        out.packed = slot[1];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SehCopySlotToTrack(uint8_t* track, const CustomJacketSlot& src)
{
    __try
    {
        auto* slot = static_cast<uint64_t*>(SpecialTrackHelpers::HeapAllocZero(0x10));
        if (!slot) return false;
        slot[0] = src.target;
        slot[1] = src.packed;
        reinterpret_cast<uint64_t*>(src.target)[5] += 1;
        *reinterpret_cast<uint64_t**>(track + 0x50) = slot;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SehTriggerLoad(uint8_t* track, const CustomJacketSlot& slot)
{
    __try
    {
        uint64_t ctx = slot.packed & 0x000FFFFFFFFFFFFFULL;
        if (!ctx) return false;
        uint64_t* vt = *reinterpret_cast<uint64_t**>(ctx);
        uint64_t** slotField = reinterpret_cast<uint64_t**>(track + 0x50);
        using Fn = void(__fastcall*)(void*, uint64_t**, uint64_t);
        reinterpret_cast<Fn>(vt[8])(reinterpret_cast<void*>(ctx), slotField, 1);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SehTriggerDetachedLoad(const CustomJacketSlot& slot)
{
    __try
    {
        auto* temp = static_cast<uint64_t*>(SpecialTrackHelpers::HeapAllocZero(0x10));
        if (!temp) return false;
        temp[0] = slot.target;
        temp[1] = slot.packed;
        auto** slotField = static_cast<uint64_t**>(SpecialTrackHelpers::HeapAllocZero(sizeof(uint64_t*)));
        if (!slotField) return false;
        *slotField = temp;
        reinterpret_cast<uint64_t*>(slot.target)[5] += 1;

        uint64_t ctx = slot.packed & 0x000FFFFFFFFFFFFFULL;
        if (!ctx) return false;
        uint64_t* vt = *reinterpret_cast<uint64_t**>(ctx);
        using Fn = void(__fastcall*)(void*, uint64_t**, uint64_t);
        reinterpret_cast<Fn>(vt[8])(reinterpret_cast<void*>(ctx),
            slotField, 1);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SehAssignLoaded(void* ctx, uint64_t** slotPtr, void* loadedObj, void* target)
{
    __try
    {
        uint64_t* vt = *reinterpret_cast<uint64_t**>(ctx);
        using AssignFn = void(__fastcall*)(void*, uint64_t**, void*, void*, char);
        reinterpret_cast<AssignFn>(vt[3])(ctx, slotPtr, loadedObj, target, static_cast<char>(0x80));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace CustomJacketInternal
