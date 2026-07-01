#include "pch.h"
#include "VehicleSnapshot.h"

#include <sstream>

namespace VehicleSeatTrace {

std::string Hex(uintptr_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << value;
    return oss.str();
}

bool CaptureSnapshot(uintptr_t plugin, Snapshot& s)
{
    uintptr_t backPtr = 0;
    if (!plugin || !ReadValue(plugin + 0x150, s.rideOn) || !s.rideOn)
        return false;
    if (!ReadValue(s.rideOn + 0x88, backPtr) || backPtr != plugin)
        return false;
    if (!ReadValue(s.rideOn + 0x190, s.runtime) || !s.runtime)
        return false;

    ReadValue(plugin + 0x118, s.current);
    ReadValue(plugin + 0x11A, s.next);
    ReadValue(plugin + 0x11B, s.flag);
    ReadValue(plugin + 0x220, s.seatKey);
    ReadValue(s.rideOn + 0x180, s.elapsed);
    ReadValue(s.rideOn + 0x198, s.stage);
    ReadValue(s.runtime + 0x189, s.b189);
    ReadValue(s.runtime + 0x18A, s.b18A);
    ReadValue(s.runtime + 0x18B, s.b18B);
    ReadValue(s.runtime + 0x190, s.b190);
    ReadValue(s.runtime + 0x191, s.b191);
    ReadValue(s.runtime + 0x192, s.b192);
    ReadValue(s.runtime + 0x381, s.b381);
    ReadValue(s.runtime + 0x2A0, s.rideKind);
    ReadValue(s.runtime + 0x3B1, s.b3B1);
    return s.current <= 8 && s.next <= 8 && s.stage <= 16;
}

std::string FormatSnapshot(uintptr_t plugin, const Snapshot& s)
{
    std::ostringstream oss;
    oss << " plugin=" << Hex(plugin)
        << " cur=" << static_cast<int>(s.current)
        << " next=" << static_cast<int>(s.next)
        << " flag=" << static_cast<int>(s.flag)
        << " seatKey=" << Hex(s.seatKey)
        << " rideOn=" << Hex(s.rideOn)
        << " runtime=" << Hex(s.runtime)
        << " kind=" << s.rideKind
        << " stage=" << s.stage
        << " elapsed=" << s.elapsed
        << " b189=" << static_cast<int>(s.b189)
        << " b18A=" << static_cast<int>(s.b18A)
        << " b18B=" << static_cast<int>(s.b18B)
        << " b190=" << static_cast<int>(s.b190)
        << " b191=" << static_cast<int>(s.b191)
        << " b192=" << static_cast<int>(s.b192)
        << " b381=0x" << std::hex << static_cast<int>(s.b381) << std::dec
        << " b3B1=" << static_cast<int>(s.b3B1);
    return oss.str();
}

} // namespace VehicleSeatTrace
