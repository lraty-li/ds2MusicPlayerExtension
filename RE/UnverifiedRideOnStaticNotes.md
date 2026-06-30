# Unverified RideOn Static Notes

These notes are static IDA observations only. They are not runtime-verified and must not be treated as project knowledge until a plugin run confirms them.

2026-06-29 update: runtime observation has verified that `DSPlayerVehicleRideOnState_Update`
changes `plugin + 0x11A` from `1` to `2` during normal RideOn completion. The exact
instruction responsible for that write is still only a static candidate in this file.

2026-06-29 update: runtime observation has verified that
`RideVehicleRuntime_CheckAbortAndRequestState5` returns `0` during the observed normal
RideOn flow, both before and after `rideRuntime + 0x18B` becomes `1`.

## RideOnState Update Completion Gates

- `DSPlayerVehicleRideOnState_Update` (`0x140F99C40`) appears to accumulate update delta into `state + 0x180` at function entry.
- The update appears to evaluate the Drive transition path only when `*(dword *)(state + 0x198) == 2`.
- The candidate normal completion write is:

```cpp
*(word *)(*(qword *)(state + 0x88) + 0x11A) = 2;
```

Instruction: `0x140F9A2C7`.

- Before that write, the update calls `sub_140DBEA00(*(qword *)(state + 0x98), 0xED)` at `0x140F99FCC`.
- If that player query returns true, control appears to jump to `0x140F9A0B7`.
- If that query returns false, the update checks bytes at `rideRuntime + 0x190` and `rideRuntime + 0x192`.
- If both bytes are nonzero, control appears to jump to `0x140F9A0B7`.
- If either byte is zero, the update compares elapsed time at `state + 0x180` with `dword_143461CCC` at `0x140F99FFC`.
- If elapsed time is greater than or equal to `dword_143461CCC`, control appears to jump to `0x140F9A0B7`.
- Only when elapsed time is below `dword_143461CCC` does the update appear to evaluate this blocker chain:
  - `RideVehicleRuntime_CheckAbortAndRequestState5` (`0x141009C50`)
  - object flag `(*(qword *)(state + 0x90))->0x2A8 + 0x5D0 bit 0x04`
  - `rideRuntime + 0x37D bit 0x04`
  - `DSPlayerVehicleRideOnState_CanEarlyFinishRideOn` (`0x141011480`)
  - `rideRuntime + 0x18B != 0`
  - global `qword_14623E3C0 + 0x160 bit 0x40000000000000` clear
- If that blocker chain passes, the update appears to write plugin next state `3` at `0x140F9A0A9`, not state `2`.

## ProcessVehicleAttach Stage Fields

Static local reading of `DSPlayerVehicleRideOnState_ProcessVehicleAttach` (`0x140F9A370`) suggests:

- Stage 1 success path at `0x140F9AC52` writes `rideRuntime + 0x189 = 1`.
- The same path sets `rideRuntime + 0x381 bit 0x02` at `0x140F9AC60`.
- The same path writes `state + 0x198 = 2` at `0x140F9AC67`.
- Stage 2 pre-attach path requires `rideRuntime + 0x18B == 0`, calls `sub_140F8E8D0(..., dl=1, r8=0)`, then writes `rideRuntime + 0x18B = 1` at `0x140F9A53B`.
- The known attach call is `Entity_AttachToParentAndNotify(playerEntity, resolvedVehicleSeatObject, 0)` at `0x140F9AD80`.
- After attach, the helper writes `[state + 0xA8] + 0xC68 = resolvedVehicleSeatObject + 0x320`, calls `sub_140F9B4A0`, and calls `sub_140F9B670`.
- The helper later writes `state + 0x198 = 1` at `0x140F9AFEE`.

## Vtable And Command Dispatch

- `DSPlayerVehicleRideOnState` vtable appears to begin at `0x14325B730`.
- Relevant observed slots:
  - `+0x58`: `DSPlayerVehicleRideOnState_OnEnter` (`0x140F98CE0`)
  - `+0x60`: `DSPlayerVehicleRideOnState_OnExit` (`0x140F99990`)
  - `+0x68`: `DSPlayerVehicleRideOnState_CheckOrCancelOnNoSeat` (`0x140F99BE0`)
  - `+0x70`: `DSPlayerVehicleRideOnState_Update` (`0x140F99C40`)
  - `+0xD8`: `DSPlayerVehicleRideOnState_ProcessVehicleAttach` (`0x140F9A370`)
- The generic state command dispatcher at `0x14101CD90` maps command `6` to vtable slot `+0xD8`.

## OnEnter And OnExit Gate Bytes

Static local reading suggests:

- `DSPlayerVehicleRideOnState_OnEnter` resets:
  - `state + 0x198 = 0`
  - `state + 0x180 = 0`
- `OnEnter` sets `rideRuntime + 0x190 = 1` only when `*(byte *)(*(qword *)(state + 0xA8) + 0x3970) == 9`.
- `OnEnter` sets `rideRuntime + 0x191 = 1` near the end of the function.
- `Update` checks `rideRuntime + 0x190` and `rideRuntime + 0x192`, not `rideRuntime + 0x191`, before bypassing the timer gate.
- `OnExit` clears `rideRuntime + 0x191` and `rideRuntime + 0x192`.

## RideRuntime Helper Observations

Static local helper reading suggests:

- `RideRuntime_UpdateBaggageEventAndSeatFlags` (`0x141011BF0`) is called from `ProcessVehicleAttach` with `rcx = rideRuntime` and `edx = 0`.
- In that helper, when `*(dword *)(rideRuntime + 0x2A0) == 1`, it writes two adjacent bytes on the object loaded from `[qword_14623E948 + 0x24290]`:
  - `+0xBE = 1`
  - `+0xBF = seat-derived byte`
- The inspected helper did not show a direct write to `rideRuntime + 0x192`.
