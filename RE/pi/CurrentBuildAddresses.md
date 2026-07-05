# Current Build Address Mapping

## Scope

This document records verified function addresses in the current game build.
All addresses are verified via IDA MCP inspection. The RE docs at `RE/` level may
reference older builds.

## Key Finding: Address Shift

The game build has shifted compared to the addresses documented in older RE notes.
ALL addresses in `VehicleBoardingAnalysis.md`, `RideOnCompletionGates.md`, and
`RideOnRealtimeAnimationArchitecture.md` should be cross-referenced against this
document for the current build.

## RideOnState Vtable (0x14325b730)

Updated vtable entries (verified 2026-07-05):

| Slot | Address | Role |
|------|---------|------|
| [0] | 0x1400A10E0 | (noop/empty) |
| [1] | 0x1400A10E0 | (noop/empty) |
| [2] | 0x1400A10E0 | (noop/empty) |
| [3] | 0x140F98D00 | **OnEnter** (was 0x140F98CE0) |
| [4] | 0x140F999B0 | **OnExit** (was 0x140F99990) |
| [5] | 0x140F99C00 | Seat classification helper |
| [6] | 0x140F99C60 | **Update** (was 0x140F99C40) |
| [7] | 0x1400A10E0 | (noop/empty) |
| [8-10] | 0x1400A10E0 | (noop/empty) |
| [11] | 0x140F9B070 | RideOnState_UpdateSeatPoseRequest |
| [12-16] | 0x1400A10E0 | (noop/empty) |
| [17] | 0x140F9A390 | **ProcessVehicleAttach** (was 0x140F9A370) |

## Core Function Addresses

| Function | Old Address | Current Address |
|----------|-------------|-----------------|
| RideOnState_OnEnter | 0x140F98CE0 | **0x140F98D00** |
| RideOnState_OnExit | 0x140F99990 | **0x140F999B0** |
| RideOnState_Update | 0x140F99C40 | **0x140F99C60** |
| ProcessVehicleAttach | 0x140F9A370 | **0x140F9A390** |
| Entity_AttachToParentAndNotify | 0x140130900 | 0x140130900 (unchanged) |
| MountableComponent_StartMount | 0x1402F1EF0 | 0x1402F1EF0 (unchanged) |

## RideOn Completion Gates (Current Build)

### Gate 1: Entity query
```c
sub_140DBEA20(playerEntity, 0xED)  // was sub_140DBEA00
```

### Gate 2: rideRuntime flags
```c
rideRuntime + 0x190 (b190) && rideRuntime + 0x192 (b192)
```

### Gate 3: Timer constant
```c
dword_143461D0C  // was dword_143461CCC
```

## Helper Function Addresses

| Function | Old Address | Current Address |
|----------|-------------|-----------------|
| RideRuntime_UpdateSeatAnimationData | 0x141012280 | **0x1410122A0** |
| Entity_QueryBoolById (Gate 1) | 0x140DBEA00 | **0x140DBEA20** |
| RideRuntime_SetRideOnActionSlotFilters | 0x1410139A0 | **0x1410139C0** |
| RideOnState_UpdateSeatPoseRequest | 0x140F9B070 | 0x140F9B090 (approx) |
| PresentationGlobal_RequestAction | 0x140E21860 | 0x140E21990 (likely) |
| AnimSetState wrapper | 0x140DB9A10 | remains near that range |
| AnimFloat544 setter | 0x140DBA820 | remains near that range |

## RideOnState_Update Prologue (0x140F99C60)

```
0x140F99C60: 40 53                   push rbx (REX prefix)
0x140F99C62: 56                      push rsi
0x140F99C63: 57                      push rdi
0x140F99C64: 48 83 EC 70             sub rsp, 0x70
0x140F99C68: C5 F2 58 81 80 01 00 00 vaddss xmm0, xmm1, [rcx+0x180]
0x140F99C70: C5 FA 11 81 80 01 00 00 vmovss [rcx+0x180], xmm0
```

## Critical Hook Bug (RESOLVED)

The pattern scanner finds the vaddss at offset 8 into the function, computes
`patchLen = 8`, and copies the first 8 bytes to the trampoline:

```
40 53 56 57 48 83 EC 70 C5
```

The C5 at byte 7 is the VEX prefix of vaddss. When the trampoline executes these
8 bytes, the C5 is followed by the absolute jump bytes (50 48 B8 ...), which the CPU
decodes as a DIFFERENT VEX instruction. This corrupts XMM state and causes the hook
to malfunction.

**Fix**: patchLen must be >= 16 to include the full 8-byte vaddss instruction.

## OnEnter Key Structure (0x140F98D00)

Same logical structure as old build:
1. Reset stage, elapsed, rideRuntime fields
2. Set b190 when boarding context == 9
3. Set state+0x19C when context == 3 or 9
4. Write parameter envelope to rideOn+0x98
5. Call animComponent+0x250(xmm1=0)
6. Call animComponent+0x20(state=5)
7. Set b191 = 1
8. sub_140E21990(..., 1314714398)
