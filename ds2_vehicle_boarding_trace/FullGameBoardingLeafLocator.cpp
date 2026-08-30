#include "pch.h"
#include "FullGameBoardingLeafLocator.h"

#include "PatternScan.h"

#include <array>

namespace FullGameBoardingLeafLocator {
namespace {

constexpr const char* kPrimarySignature =
    "3D 58 A7 C4 0B 0F 85 ? ? ? ? 41 80 BE BB 07 05 00 00 "
    "0F 84 ? ? ? ? 48 8B 95 30 27 00 00 C6 44 24 20 01 "
    "48 8B 8C 24 ? ? ? ? 45 31 C0 41 0F 28 D9 FF 15 ? ? ? ?";
constexpr const char* kVerifierSignature =
    "3D 58 A7 C4 0B 0F 85 ? ? ? ? 41 80 BE CE B8 05 00 00 "
    "0F 84 ? ? ? ? 48 8B 84 24 ? ? ? ? 48 8B 8C 24 ? ? ? ? "
    "4C 8D 2C 08 49 83 C5 38 48 8B 95 78 34 00 00 C6 44 24 20 01 "
    "4C 89 E9 45 31 C0 41 0F 28 D9 FF 15 ? ? ? ?";
constexpr std::array<const char*, 5> kRequiredBranchSignatures = {
    "41 80 BE E1 07 05 00 00 0F 84 ? ? ? ? 48 8B 95 20 27 00 00 "
    "C6 44 24 20 01 48 8B 8C 24 ? ? ? ? 45 31 C0 41 0F 28 D9 "
    "FF 15 ? ? ? ?",
    "41 80 BE BA 07 05 00 00 0F 84 ? ? ? ? 48 8B 95 18 27 00 00 "
    "C6 44 24 20 01 48 8B 8C 24 ? ? ? ? 45 31 C0 41 0F 28 D9 "
    "FF 15 ? ? ? ?",
    "41 80 BE E2 07 05 00 00 0F 84 ? ? ? ? 48 8B 95 B8 26 00 00 "
    "C6 44 24 20 01 48 8B 8C 24 ? ? ? ? 45 31 C0 41 0F 28 D9 "
    "FF 15 ? ? ? ?",
    "41 80 BE B8 07 05 00 00 0F 84 ? ? ? ? 48 8B 95 C8 26 00 00 "
    "C6 44 24 20 01 48 8B 8C 24 ? ? ? ? 45 31 C0 41 0F 28 D9 "
    "FF 15 ? ? ? ?",
    "41 80 BE BC 07 05 00 00 0F 84 ? ? ? ? 48 8B 95 10 27 00 00 "
    "C6 44 24 20 01 48 8B 8C 24 ? ? ? ? 45 31 C0 41 0F 28 D9 "
    "FF 15 ? ? ? ?"};
constexpr uint32_t kPrimaryCallOffset = 0x34;
constexpr uint32_t kVerifierCallOffset = 0x47;
constexpr uint32_t kBranchCallOffset = 0x29;
constexpr uint32_t kIndirectCallSize = 6;

bool ResolveReturn(
    uintptr_t pattern, uint32_t callOffset, uintptr_t expectedSlot,
    uintptr_t& callerReturn)
{
    if (!pattern)
        return false;
    const uintptr_t call = pattern + callOffset;
    if (PatternScan::ResolveRip(call, 2) != expectedSlot)
        return false;
    callerReturn = call + kIndirectCallSize;
    return true;
}

} // namespace

bool Locate(HMODULE module, Result& result)
{
    result = {};
    if (!PatternScan::GetSection(
            module, ".text", result.textStart, result.textSize)) {
        return false;
    }

    const uintptr_t primary = PatternScan::FindUnique(
        result.textStart, result.textSize, kPrimarySignature);
    const uintptr_t verifier = PatternScan::FindUnique(
        result.textStart, result.textSize, kVerifierSignature);
    if (!primary || !verifier)
        return false;

    const uintptr_t primaryCall = primary + kPrimaryCallOffset;
    result.slotAddress = PatternScan::ResolveRip(primaryCall, 2);
    result.callerReturns[0] = primaryCall + kIndirectCallSize;
    uintptr_t verifierReturn = 0;
    if (!ResolveReturn(
            verifier, kVerifierCallOffset, result.slotAddress,
            verifierReturn)) {
        return false;
    }

    for (size_t index = 0; index < kRequiredBranchSignatures.size(); ++index) {
        const uintptr_t branch = PatternScan::FindUnique(
            result.textStart, result.textSize,
            kRequiredBranchSignatures[index]);
        if (!ResolveReturn(
                branch, kBranchCallOffset, result.slotAddress,
                result.callerReturns[index + 1])) {
            return false;
        }
    }

    return true;
}

} // namespace FullGameBoardingLeafLocator
