#include "pch.h"

#include "CustomJacketPixelTest.h"

#include "CustomJacketInternal.h"
#include "HookUtils.h"

#include <sstream>

namespace
{
void* g_track = nullptr;
bool g_applied = false;
uint64_t g_target = 0;
bool g_uiCloneApplied = false;

struct SlotChoice
{
    const char* name = "";
    uint32_t offset = 0;
    uint32_t index = UINT32_MAX;
    uint64_t slotAddr = 0;
    CustomJacketSlot slot = {};
};

struct ArrayHeader
{
    uint32_t count;
    uint32_t capacity;
    uint64_t data;
};

bool TryReadArrayHeader(uint64_t base, uint32_t offset, ArrayHeader& out)
{
    uint64_t first = 0;
    uint64_t data = 0;
    if (!CustomJacketInternal::SehReadU64(base + offset, first)
        || !CustomJacketInternal::SehReadU64(base + offset + 8, data))
    {
        return false;
    }

    out.count = static_cast<uint32_t>(first);
    out.capacity = static_cast<uint32_t>(first >> 32);
    out.data = data;
    return out.count <= out.capacity && out.count < 0x10000 && out.data;
}

bool IsValidSlot(const CustomJacketSlot& slot)
{
    if (!slot.target || !slot.packed) return false;

    const uint64_t ctx = slot.packed & 0x000FFFFFFFFFFFFFULL;
    uint64_t vt = 0;
    uint64_t flagsFn = 0;
    uint64_t targetHead = 0;
    return ctx > 0x100000000ull
        && CustomJacketInternal::SehReadU64(ctx, vt)
        && CustomJacketInternal::SehReadU64(vt + 8 * 8, flagsFn)
        && CustomJacketInternal::SehReadU64(slot.target, targetHead)
        && targetHead;
}

bool TryReadSlotPointer(uint64_t addr, uint64_t& slotAddr, CustomJacketSlot& slot)
{
    if (!CustomJacketInternal::SehReadU64(addr, slotAddr) || !slotAddr)
    {
        return false;
    }
    return CustomJacketInternal::SehReadSlot(slotAddr, slot) && IsValidSlot(slot);
}

bool TryReadInlineSlot(uint64_t addr, uint64_t& slotAddr, CustomJacketSlot& slot)
{
    if (!CustomJacketInternal::SehReadSlot(addr, slot) || !IsValidSlot(slot))
    {
        return false;
    }
    slotAddr = addr;
    return true;
}

bool TryReadCatalogueSlot(void* catalogue, const char* name, uint32_t offset, SlotChoice& out)
{
    const uint64_t addr = reinterpret_cast<uint64_t>(catalogue) + offset;
    uint64_t slotAddr = 0;
    CustomJacketSlot slot = {};
    if (!TryReadSlotPointer(addr, slotAddr, slot) && !TryReadInlineSlot(addr, slotAddr, slot))
    {
        return false;
    }

    out.name = name;
    out.offset = offset;
    out.slotAddr = slotAddr;
    out.slot = slot;
    return true;
}

bool TryReadArraySlot(void* catalogue, const char* name, uint32_t offset,
    uint32_t index, SlotChoice& out)
{
    ArrayHeader array = {};
    const uint64_t base = reinterpret_cast<uint64_t>(catalogue);
    if (!TryReadArrayHeader(base, offset, array) || index >= array.count)
    {
        return false;
    }

    uint64_t slotAddr = 0;
    CustomJacketSlot slot = {};
    const uint64_t ptrItem = array.data + index * 8ull;
    const uint64_t inlineItem = array.data + index * 16ull;
    if (!TryReadSlotPointer(ptrItem, slotAddr, slot)
        && !TryReadInlineSlot(inlineItem, slotAddr, slot))
    {
        return false;
    }

    out.name = name;
    out.offset = offset;
    out.index = index;
    out.slotAddr = slotAddr;
    out.slot = slot;
    return true;
}

bool PickJacketSlot(void* catalogue, SlotChoice& out)
{
    const struct ArrayCandidate
    {
        const char* name;
        uint32_t offset;
        uint32_t maxScan;
    } arrays[] = {
        {"HotSpringImageTextures", 0x50, 7},
        {"ConstructionHoloImageTextures", 0x70, 12},
        {"MissionImageTextures", 0x30, 12},
    };

    for (const auto& array : arrays)
    {
        for (uint32_t i = 0; i < array.maxScan; ++i)
        {
            if (TryReadArraySlot(catalogue, array.name, array.offset, i, out))
            {
                return true;
            }
        }
    }

    const struct Candidate
    {
        const char* name;
        uint32_t offset;
    } candidates[] = {
        {"DefaultConstructionHoloImageTexture", 0xC8},
        {"DefaultCostumeCustomizeImageTexture", 0xD0},
        {"DefaultMissionImageTexture", 0xA8},
        {"DefaultHotSpringImageTexture", 0xB8},
        {"DefaultMusicJacketImageTexture", 0xC0},
    };

    for (const auto& candidate : candidates)
    {
        if (TryReadCatalogueSlot(catalogue, candidate.name, candidate.offset, out))
        {
            return true;
        }
    }
    return false;
}

DWORD WINAPI ProbeThread(LPVOID param)
{
    auto* logger = static_cast<Logger*>(param);

    for (uint32_t i = 0; i < 30; ++i)
    {
        Sleep(1000);
        if (!g_target) return 0;

        uint64_t loaded = 0;
        if (!CustomJacketInternal::SehReadU64(g_target + 0x20, loaded) || !loaded)
        {
            continue;
        }

        logger->Log(std::string("tick=") + std::to_string(i + 1)
            + " loaded=" + HookUtils::HexU64(loaded));

        uint64_t slotAddr = 0;
        CustomJacketSlot slot = {};
        uint64_t texture = 0;
        if (CustomJacketInternal::SehReadU64(
            reinterpret_cast<uint64_t>(static_cast<uint8_t*>(g_track) + 0x50), slotAddr)
            && CustomJacketInternal::SehReadSlot(slotAddr, slot)
            && CustomJacketInternal::SehReadU64(loaded + 0x30, texture)
            && texture)
        {
            CustomJacketInternal::DumpResourceJacketProbeOnce(slotAddr, slot, loaded, texture, *logger);
        }

        if (!g_uiCloneApplied)
        {
            uint64_t newTarget = 0;
            uint64_t noDataPixelBuffer = 0;
            if (!CustomJacketInternal::TryGetAlternateJacketPixelBuffer(noDataPixelBuffer, *logger))
            {
                continue;
            }

            uint64_t sourcePixelBuffer = 0;
            CustomJacketPixelBufferInfo pbInfo = {};
            if (!texture
                || !CustomJacketInternal::SehReadU64(texture + 0x20, sourcePixelBuffer)
                || !sourcePixelBuffer
                || !CustomJacketInternal::ProbePixelBuffer(sourcePixelBuffer, pbInfo)
                || pbInfo.dxbcPages == 0)
            {
                logger->Log("uiclone: waiting for source PB DXBC pages");
                continue;
            }

            if (CustomJacketInternal::CloneLoadedUiTextureToTrack(g_track, newTarget, *logger))
            {
                g_target = newTarget;
                g_uiCloneApplied = true;
            }
        }
        if (g_uiCloneApplied) return 0;
    }
    return 0;
}
} // namespace

namespace CustomJacketPixelTest
{
void Reset()
{
    g_track = nullptr;
    g_applied = false;
    g_target = 0;
    g_uiCloneApplied = false;
    CustomJacketInternal::ResetResourceProbeDiagnostics();
    CustomJacketInternal::ResetPixelBufferDiagnostics();
    CustomJacketInternal::ResetAlternateJacketTextureProbe();
}

void TrackCreated(void* track)
{
    g_track = track;
    g_applied = false;
}

void TryApply(void* catalogueResource, const Logger& logger)
{
    if (!g_track || g_applied || !catalogueResource) return;
    CustomJacketInternal::DumpCatalogueResourceProbeOnce(catalogueResource, logger);
    CustomJacketInternal::DumpTrackAlbumProbeOnce(g_track, logger);
    CustomJacketInternal::PrepareAlternateJacketTextureProbe(catalogueResource, logger);

    SlotChoice choice = {};
    if (!PickJacketSlot(catalogueResource, choice))
    {
        logger.Log("custom jacket: no usable catalogue UITexture slot");
        return;
    }

    if (!CustomJacketInternal::SehCopySlotToTrack(static_cast<uint8_t*>(g_track), choice.slot))
    {
        logger.Log("custom jacket: failed to copy slot");
        return;
    }

    CustomJacketInternal::SehTriggerLoad(static_cast<uint8_t*>(g_track), choice.slot);
    g_applied = true;
    g_target = choice.slot.target;

    std::ostringstream oss;
    oss << "custom jacket applied: source=" << choice.name
        << " offset=0x" << std::hex << choice.offset
        << " index=" << std::dec
        << (choice.index == UINT32_MAX ? -1 : static_cast<int>(choice.index))
        << " slot=" << HookUtils::HexU64(choice.slotAddr)
        << " target=" << HookUtils::HexU64(choice.slot.target);
    logger.Log(oss.str());

    HANDLE thread = CreateThread(nullptr, 0, ProbeThread,
        const_cast<Logger*>(&logger), 0, nullptr);
    if (thread) CloseHandle(thread);
}
} // namespace CustomJacketPixelTest
