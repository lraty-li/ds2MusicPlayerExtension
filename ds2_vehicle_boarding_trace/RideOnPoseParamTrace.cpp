#include "pch.h"
#include "RideOnPoseParamTrace.h"
#include "VehicleSnapshot.h"

#include <cstdint>
#include <sstream>

namespace RideOnPoseParamTrace {
namespace {

template <typename T>
T ReadOr(uintptr_t addr, T fallback)
{
    T value = fallback;
    VehicleSeatTrace::ReadValue(addr, value);
    return value;
}

void AppendFloat(std::ostringstream& oss, const char* name, uintptr_t base, uintptr_t offset)
{
    oss << ' ' << name << '=' << ReadOr<float>(base + offset, 0.0f);
}

void AppendU32(std::ostringstream& oss, const char* name, uintptr_t base, uintptr_t offset)
{
    oss << ' ' << name << '=' << ReadOr<uint32_t>(base + offset, 0);
}

void AppendFlag(std::ostringstream& oss, const char* name, uintptr_t base, uintptr_t offset)
{
    oss << ' ' << name << '=' << static_cast<int>(ReadOr<uint8_t>(base + offset, 0));
}

} // namespace

void LogRideOnPoseParams(const Logger& logger, const char* label, uintptr_t rideOn)
{
    const uintptr_t plugin = ReadOr<uintptr_t>(rideOn + 0x88, 0);
    const uintptr_t params = ReadOr<uintptr_t>(rideOn + 0x98, 0);
    const uintptr_t runtime = ReadOr<uintptr_t>(rideOn + 0x190, 0);
    if (!params)
        return;

    std::ostringstream oss;
    oss << "RideOnPoseParams " << label
        << " rideOn=" << VehicleSeatTrace::Hex(rideOn)
        << " plugin=" << VehicleSeatTrace::Hex(plugin)
        << " params=" << VehicleSeatTrace::Hex(params)
        << " runtime=" << VehicleSeatTrace::Hex(runtime);

    if (runtime) {
        AppendU32(oss, "kind", runtime, 0x2A0);
        AppendU32(oss, "variant", runtime, 0x2A4);
        AppendFlag(oss, "b18A", runtime, 0x18A);
        AppendFlag(oss, "b18B", runtime, 0x18B);
        AppendFlag(oss, "b191", runtime, 0x191);
    }

    AppendFloat(oss, "f3830", params, 0x3830);
    AppendFloat(oss, "f3890", params, 0x3890);
    AppendFloat(oss, "f3920", params, 0x3920);
    AppendFloat(oss, "f3950", params, 0x3950);
    AppendFloat(oss, "f3980", params, 0x3980);
    AppendFloat(oss, "f3A70", params, 0x3A70);
    AppendFloat(oss, "f3DD0", params, 0x3DD0);
    AppendFloat(oss, "f53C0", params, 0x53C0);
    AppendFlag(oss, "fl1030", params, 0x1030);
    AppendFlag(oss, "fl1090", params, 0x1090);
    AppendFlag(oss, "fl1120", params, 0x1120);
    AppendFlag(oss, "fl1510", params, 0x1510);
    AppendFlag(oss, "fl3910", params, 0x3910);
    AppendFlag(oss, "fl3970", params, 0x3970);

    logger.Log(oss.str());
}

} // namespace RideOnPoseParamTrace
