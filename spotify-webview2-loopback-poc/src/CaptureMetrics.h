#pragma once

#include <Windows.h>
#include <cstdint>

constexpr UINT kCaptureMetricsMessage = WM_APP + 17;
constexpr UINT kDiagnosticToneMessage = WM_APP + 18;
constexpr UINT kDiagnosticMuteMessage = WM_APP + 19;
constexpr UINT kDiagnosticMetricsMessage = WM_APP + 20;
constexpr UINT kDiagnosticLogMessage = WM_APP + 21;

struct CaptureMetrics
{
    bool active = false;
    HRESULT error = S_OK;
    double rms = 0.0;
    double peak = 0.0;
    double nonzeroRatio = 0.0;
    uint64_t totalFrames = 0;
    uint64_t qpcPosition = 0;
    uint32_t silentPackets = 0;
    uint32_t discontinuities = 0;
    uint32_t timestampErrors = 0;
};
