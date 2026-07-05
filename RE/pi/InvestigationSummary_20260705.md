# 2026-07-05 Investigation Summary

## What Was Found

### 1. Game build address shift
The current game build has shifted all RideOn-related function addresses.
Our old RE docs were based on a previous build. See `CurrentBuildAddresses.md`
for the complete updated mapping.

### 2. Root cause of the non-functional hook: VEX trampoline bug
The pattern scanner computed `patchLen = 8` for `RideOnState_Update`, copying
only 8 bytes to the trampoline. Byte 7 is `C5` — the VEX prefix of the 8-byte
`vaddss` instruction. The trampoline executed this partial prefix followed by
the absolute jump bytes, which the CPU decoded as a different VEX instruction,
corrupting XMM state. The corrupted float state silently broke the function.

See `TrampolineVexBug.md` for the full analysis.

### 3. Correct addresses (current build)

| Function | Address |
|----------|---------|
| RideOnState_OnEnter | 0x140F98D00 |
| RideOnState_Update | 0x140F99C60 |
| ProcessVehicleAttach | 0x140F9A390 |

The pattern scanner at runtime resolves RideOnState_Update correctly (matches
the vaddss/vmovss pattern with displacement 0x180). The bug was only in the
patchLen computation, not the address resolution.

## Fixes Applied

### AddressResolver.cpp
Changed `patchLen = min(dist, 16)` to `patchLen = max(16, dist)` — ensures
trampoline doesn't split VEX instructions.

### Hooks.cpp
Replaced unconditional `ForceNextDrive` with conditional `TryRequestDrive`:
- Checks `current==1, next==1, stage==2, b18A==1, b191==1`
- Only writes `next=2` when all conditions are met
- Added detailed `UpdateTick` logging for debugging

## What To Expect When Testing

With the fixed hook:
1. Log should show `UpdateTick #1` with state information when player boards
2. If conditions are met, `FastDrive requested` should appear at ~0.05s
3. Player should transition to Drive without the ~3.6s animation
4. A brief pose settling artifact may be visible (documented in previous experiments)

If conditions are NOT met (e.g., b18A or b191 is 0), the hook won't trigger
and normal boarding will proceed. This is safer than the old unconditional
approach.

## New RE/pi Documents

- `CurrentBuildAddresses.md` — complete address mapping for current build
- `TrampolineVexBug.md` — root cause of the broken hook
- `FastBoardingNextSteps.md` — exploration strategy for next development phase
- `RideOnCompletionGates.md` (existing) — remains valid for gate logic
- `RideOnRealtimeAnimationArchitecture.md` (existing) — animation processing architecture
