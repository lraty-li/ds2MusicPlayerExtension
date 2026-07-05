# RideOn Trampoline Bug — Root Cause Analysis

## Date: 2026-07-05

## Symptom

`ds2_vehicle_boarding_trace` installed a hook on `RideOnState_Update`, but:
- No `FastDrive tick` logs were produced
- Player could not board (pressing F produced no boarding animation)

## Root Cause: VEX Instruction Split By Trampoline

The `AddressResolver` pattern scanner found the `vaddss`/`vmovss` pattern inside
the real `RideOnState_Update` at offset 8 from the function start. It computed
`patchLen = 8` (the distance from function prologue to pattern).

### The problem

The `RideOnState_Update` prologue at `0x140F99C60` (current build):

```
0x140F99C60: 40 53                   push rbx (REX.W prefix)
0x140F99C62: 56                      push rsi
0x140F99C63: 57                      push rdi
0x140F99C64: 48 83 EC 70             sub rsp, 0x70
0x140F99C68: C5 F2 58 81 80 01 00 00 vaddss xmm0, xmm1, [rcx+0x180]  ← 8-byte VEX instruction
```

With `patchLen = 8`, the trampoline copied bytes 0-7:

```
40 53 56 57 48 83 EC 70 C5
```

Byte 7 is `C5` — the VEX.2Byte prefix of the `vaddss` instruction. When the
trampoline executes these 8 bytes, `C5` is followed by the absolute jump bytes
(`50 48 B8 ...`), which the CPU decodes as a COMPLETELY DIFFERENT VEX
instruction. This corrupts XMM register state (specifically the input float
`xmm1` which holds `delta`).

After the corrupted trampoline execution, the code at `target+8` continues with
the bytes `F2 58 81 80 01 00 00` — but these are now executing with the XMM
state already corrupted by the wrong instruction. The `vaddss` computes garbage
for the elapsed timer.

The entire RideOn state machine then operates with corrupted float state,
causing silent failure of the boarding action.

### Why there were no logs

The trampoline itself doesn't crash — the CPU decodes the corrupted bytes as
valid (but wrong) instructions. The corrupted XMM state causes the game to
silently malfunction, and since the hook runs AFTER the original function (which
itself is broken by the corrupted trampoline), the hook's `TryRequestDrive`
never sees valid state. The `__try/__except` silently catches the access
violation when reading from corrupted pointers.

## The Fix

Changed `updatePatchLen` from `min(dist, 16)` to `max(16, dist)`:

```cpp
// Before (broken):
out.updatePatchLen = dist < 16 ? dist : 16;

// After (fixed):
out.updatePatchLen = dist < 16 ? 16 : dist;
```

With `patchLen = 16`, the trampoline copies ALL 16 bytes (full prologue + full
vaddss instruction) and jumps to `target+16` where the `vmovss` instruction
starts. No instruction is split.

## Impact

This bug affects ALL hooks that intercept functions where the first AVX
instruction after the prologue is split by the trampoline copy. For the
`RideOnState_Update` specifically, the `C5 F2 58 81` vaddss instruction at
offset 8 extends through byte 15, so any patchLen < 16 splits it.

## Prevention

When creating hooks for functions with AVX/VEX instructions near the prologue:
1. Identify the first instruction boundary AFTER the minimum patchLen
2. Ensure patchLen rounds up to the next complete instruction boundary
3. For VEX instructions: 2-byte VEX prefix + up to 4 operand bytes = 6 bytes min;
   3-byte VEX prefix (C4) + up to 4 operand bytes = 7 bytes min
