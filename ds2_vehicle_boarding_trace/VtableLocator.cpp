#include "pch.h"
#include "VtableLocator.h"

#include "HookUtils.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <cstdint>

namespace VtableLocator {
namespace {

constexpr size_t kMaximumSlotIndex = 96;

struct CompleteObjectLocator {
    uint32_t signature;
    uint32_t offset;
    uint32_t constructorDisplacement;
    int32_t typeDescriptorRva;
    int32_t classDescriptorRva;
    int32_t selfRva;
};

bool IsInImage(
    HMODULE module, DWORD imageSize, uintptr_t address, size_t size)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    const uintptr_t end = base + imageSize;
    const uintptr_t rangeEnd = address + size;
    return address >= base && rangeEnd >= address && rangeEnd <= end;
}

bool ReadTypeName(
    HMODULE module, DWORD imageSize, uintptr_t colAddress,
    Match& match)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    if (!IsInImage(
            module, imageSize, colAddress, sizeof(CompleteObjectLocator))) {
        return false;
    }
    const auto* col = reinterpret_cast<const CompleteObjectLocator*>(colAddress);
    if (col->signature != 1 || base + col->selfRva != colAddress)
        return false;

    const uintptr_t typeDescriptor = base + col->typeDescriptorRva;
    constexpr size_t kTypeHeaderSize = sizeof(uintptr_t) * 2;
    if (!IsInImage(
            module, imageSize, typeDescriptor, kTypeHeaderSize + 4)) {
        return false;
    }
    const char* name = reinterpret_cast<const char*>(
        typeDescriptor + kTypeHeaderSize);
    if (name[0] != '.' || name[1] != '?' || name[2] != 'A')
        return false;

    std::string value;
    for (size_t i = 0; i < 160; ++i) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(name + i);
        if (!IsInImage(module, imageSize, address, 1))
            return false;
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        if (!ch) {
            match.col = colAddress;
            match.subobjectOffset = col->offset;
            match.typeName = value;
            return !value.empty();
        }
        if (ch < 0x20 || ch > 0x7E)
            return false;
        value.push_back(static_cast<char>(ch));
    }
    return false;
}

bool ResolveCandidate(
    HMODULE module, DWORD imageSize, uintptr_t targetSlot,
    Match& match)
{
    for (size_t index = 0; index <= kMaximumSlotIndex; ++index) {
        const uintptr_t vtable = targetSlot - index * sizeof(uintptr_t);
        uintptr_t colAddress = 0;
        if (!VehicleSeatTrace::ReadValue(
                vtable - sizeof(uintptr_t), colAddress) || !colAddress) {
            continue;
        }
        Match current = {};
        if (!ReadTypeName(module, imageSize, colAddress, current))
            continue;
        current.vtable = vtable;
        current.slot = targetSlot;
        current.slotIndex = static_cast<uint32_t>(index);
        match = current;
        return true;
    }
    return false;
}

} // namespace

bool FindUnique(HMODULE module, const char* signature, Match& match)
{
    match = {};
    uintptr_t textStart = 0;
    size_t textSize = 0;
    uintptr_t rdataStart = 0;
    size_t rdataSize = 0;
    DWORD imageSize = 0;
    if (!PatternScan::GetSection(module, ".text", textStart, textSize) ||
        !PatternScan::GetSection(module, ".rdata", rdataStart, rdataSize) ||
        !HookUtils::TryGetModuleSize(module, imageSize)) {
        return false;
    }
    match.target = PatternScan::FindUnique(textStart, textSize, signature);
    if (!match.target)
        return false;

    Match resolved = {};
    for (size_t offset = 0; offset + sizeof(uintptr_t) <= rdataSize;
         offset += sizeof(uintptr_t)) {
        const uintptr_t slot = rdataStart + offset;
        if (*reinterpret_cast<const uintptr_t*>(slot) != match.target)
            continue;
        ++match.pointerMatches;
        Match candidate = {};
        if (!ResolveCandidate(module, imageSize, slot, candidate))
            continue;
        ++match.rttiMatches;
        resolved = candidate;
    }
    resolved.target = match.target;
    resolved.pointerMatches = match.pointerMatches;
    resolved.rttiMatches = match.rttiMatches;
    match = resolved;
    return match.pointerMatches == 1 && match.rttiMatches == 1;
}

bool FindUniqueByRtti(
    HMODULE module, const char* typeName, uint32_t subobjectOffset,
    uint32_t slotIndex, Match& match)
{
    match = {};
    if (!module || !typeName || !*typeName || slotIndex > kMaximumSlotIndex)
        return false;

    uintptr_t textStart = 0;
    size_t textSize = 0;
    uintptr_t rdataStart = 0;
    size_t rdataSize = 0;
    DWORD imageSize = 0;
    if (!PatternScan::GetSection(module, ".text", textStart, textSize) ||
        !PatternScan::GetSection(module, ".rdata", rdataStart, rdataSize) ||
        !HookUtils::TryGetModuleSize(module, imageSize)) {
        return false;
    }

    Match resolved = {};
    for (size_t offset = 0; offset + sizeof(uintptr_t) <= rdataSize;
         offset += sizeof(uintptr_t)) {
        const uintptr_t colSlot = rdataStart + offset;
        uintptr_t colAddress = 0;
        if (!VehicleSeatTrace::ReadValue(colSlot, colAddress) || !colAddress)
            continue;
        Match candidate = {};
        if (!ReadTypeName(module, imageSize, colAddress, candidate) ||
            candidate.typeName != typeName ||
            candidate.subobjectOffset != subobjectOffset) {
            continue;
        }

        const uintptr_t vtable = colSlot + sizeof(uintptr_t);
        const uintptr_t slot = vtable + slotIndex * sizeof(uintptr_t);
        uintptr_t target = 0;
        if (!IsInImage(module, imageSize, slot, sizeof(uintptr_t)) ||
            !VehicleSeatTrace::ReadValue(slot, target) ||
            target < textStart || target >= textStart + textSize) {
            continue;
        }

        ++match.rttiMatches;
        candidate.vtable = vtable;
        candidate.slot = slot;
        candidate.slotIndex = slotIndex;
        candidate.target = target;
        resolved = candidate;
    }

    resolved.rttiMatches = match.rttiMatches;
    match = resolved;
    return match.rttiMatches == 1;
}

bool SwapSlot(uintptr_t slotAddress, uintptr_t expected, void* replacement)
{
    auto* slot = reinterpret_cast<void* volatile*>(slotAddress);
    DWORD oldProtect = 0;
    if (!VirtualProtect(
            reinterpret_cast<void*>(slotAddress), sizeof(void*),
            PAGE_READWRITE, &oldProtect)) {
        return false;
    }
    void* replaced = InterlockedCompareExchangePointer(
        slot, replacement, reinterpret_cast<void*>(expected));
    DWORD ignored = 0;
    const bool restored = VirtualProtect(
        reinterpret_cast<void*>(slotAddress), sizeof(void*),
        oldProtect, &ignored) != FALSE;
    if (!restored && replaced == reinterpret_cast<void*>(expected)) {
        InterlockedCompareExchangePointer(
            slot, reinterpret_cast<void*>(expected), replacement);
        VirtualProtect(
            reinterpret_cast<void*>(slotAddress), sizeof(void*),
            oldProtect, &ignored);
    }
    return replaced == reinterpret_cast<void*>(expected) && restored;
}

} // namespace VtableLocator
