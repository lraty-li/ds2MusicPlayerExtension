#pragma once

#include <array>
#include <cstdint>
#include <windows.h>

namespace FullGameBoardingLeafLocator {

struct Result {
    uintptr_t textStart = 0;
    size_t textSize = 0;
    uintptr_t slotAddress = 0;
    std::array<uintptr_t, 6> callerReturns{};
};

bool Locate(HMODULE module, Result& result);

} // namespace FullGameBoardingLeafLocator
