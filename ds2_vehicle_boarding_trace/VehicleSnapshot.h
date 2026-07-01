#pragma once

#include <cstdint>
#include <string>

namespace VehicleSeatTrace {

struct Snapshot {
    uintptr_t seatKey = 0;
    uintptr_t rideOn = 0;
    uintptr_t runtime = 0;
    uint8_t current = 0;
    uint8_t next = 0;
    uint8_t flag = 0;
    uint32_t stage = 0;
    float elapsed = 0.0f;
    uint8_t b189 = 0;
    uint8_t b18A = 0;
    uint8_t b18B = 0;
    uint8_t b190 = 0;
    uint8_t b191 = 0;
    uint8_t b192 = 0;
    uint8_t b381 = 0;
    uint8_t b3B1 = 0;
    uint32_t rideKind = 0;
};

template <typename T>
bool ReadValue(uintptr_t addr, T& out)
{
    __try {
        out = *reinterpret_cast<const T*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string Hex(uintptr_t value);
bool CaptureSnapshot(uintptr_t plugin, Snapshot& s);
std::string FormatSnapshot(uintptr_t plugin, const Snapshot& s);

} // namespace VehicleSeatTrace
