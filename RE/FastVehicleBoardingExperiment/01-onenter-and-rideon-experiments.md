# Fast Vehicle Boarding Experiment - OnEnter and RideOn Experiments

## 2026-07-02 Minimal OnEnter Parameter Experiment

Targeted IDA analysis renamed and documented the relevant helper functions:

- `0x140E21970` -> `PresentationGlobal_WriteActionParam`
- `0x140E21860` -> `PresentationGlobal_RequestAction`
- `0x140F8E8D0` -> `SeatObject_SetRideMountStateBits`

`PresentationGlobal_WriteActionParam` writes an integer parameter to
`qword_14623E908 + 0xEC`, sets byte `+0x1F`, and writes a float parameter at
`+0x158` under the global presentation SRW lock. `RideOnState_OnEnter` calls it at
`0x140F99940`; the hook observes `_ReturnAddress() == 0x140F99945` for that call.

Automated validation using `test_boarding.ps1` completed launch, board, dismount,
and quit without crash. Runtime log excerpt:

```text
[20:19:28.078][VehicleBoard] Init result=1 plugin=0x3AE07CF0100 snapshot=unavailable
[20:19:28.078][VehicleBoard] RideOnEnterPresentationParam suppressed param=1314714398 caller=0x7FF72B1C9945
[20:19:28.080][VehicleBoard] ClassifyApproach result=0 ... kind=1 stage=0 ... b18A=1
[20:19:28.081][VehicleBoard] Attach ... stage 0->1 ... b18A 0->1 b18B 0->0
[20:19:28.094][VehicleBoard] Attach ... stage 1->2 ... b189 0->1 b18B 0->0
[20:19:28.095][VehicleBoard] FastDrive requested after ProcessVehicleAttach stage 2
[20:19:28.109][VehicleBoard] RideOnExit entry ... cur=1 next=2 ... stage=2 ... b18B=0
[20:19:28.109][VehicleBoard] DriveEnter entry ... cur=1 next=2 ... stage=2 ... b18B=0
[20:19:28.110][VehicleBoard] DriveEnter exit ... b18B=1 b191=1 b381=0x4
```

This confirms that suppressing the OnEnter presentation parameter write does not
break the player attach sequence or the fast transition into Drive. The automated
run does not provide visual proof that the long animation is gone; it only validates
runtime stability and state-machine completion for this reduced hook set.

Follow-up user visual feedback places this call on the RideOn path while the
visible boarding gate remains elsewhere in the animation startup chain.

## 2026-07-02 RideOn Animation Component State Experiment

`RideOnState_OnEnter` calls two methods on the object at `rideOn + 0xB0`:

```text
0x140F9987A: calls [animComponent vtable + 0x250]
0x140F9988F: calls [animComponent vtable + 0x20](animComponent, 5)
```

Runtime tracing resolved the observed automated boarding path to:

```text
animComponent vtable = 0x143233450
vtable + 0x20     -> 0x140DB9A10
vtable + 0x250    -> 0x140DBA820
inner object      -> animComponent + 0x8
inner vtable      -> 0x14324B340
inner vtable+0x168 -> 0x140EC50A0
```

`0x140DB9A10` is a wrapper:

```text
mov rcx, [rcx+8]
test rcx, rcx
jz ret
mov rax, [rcx]
jmp qword ptr [rax+0x168]
ret
```

This means the `RideOnState_OnEnter` call with argument `5` forwards to the inner
animation object. The current experiment hooks `0x140DB9A10` and suppresses only
the call whose return address is `0x140F99892` and whose state argument is `5`.

Automated validation completed launch, board, dismount, and quit without crash:

```text
[20:29:59.995][VehicleBoard] RideOnEnter entry ... fn20=0x7FF72AFE9A10 innerFn168=0x7FF72B0F80A0
[20:29:59.995][VehicleBoard] RideOnEnterAnimSetState suppressed state=5 animComponent=0x3A1FF949278
[20:29:59.997][VehicleBoard] Attach ... stage 0->1 ...
[20:30:00.012][VehicleBoard] Attach ... stage 1->2 ...
[20:30:00.012][VehicleBoard] FastDrive requested after ProcessVehicleAttach stage 2
[20:30:00.028][VehicleBoard] DriveEnter exit ... b18B=1 b191=1 b381=0x4
```

This confirms runtime safety and state-machine completion for suppressing this
specific OnEnter animation-component state request.

Follow-up automated screenshot validation with `capture_boarding_visual.ps1`
showed that this isolated `state=5` suppression still preserves the visible
boarding climb at both:

- `build/boarding_capture/current/01_board_150ms.png`
- `build/boarding_capture/current/02_board_500ms.png`

This identifies one OnEnter animation dispatch site while the visible boarding gate
continues to depend on additional setup around it.

## 2026-07-02 Seat Transition Helper Experiment

`ProcessVehicleAttach` calls `sub_141F6BDC0` at `0x140F9AD31` before
`Entity_AttachToParentAndNotify`. The function manipulates a seat/controller object
under a critical section and is also used by Drive/RideOff-related paths.

Experiment: suppress only the `ProcessVehicleAttach -> sub_141F6BDC0` start call
whose return address is `0x140F9AD36`, and return success (`1`) to the caller.

Runtime result:

```text
SeatTransition call caller=0x...B1CAD36 ... start=1 finishFlag=0 ... suppressed-return=1
Attach ... stage 0->1 ...
Attach ... stage 1->2 ...
FastDrive requested after ProcessVehicleAttach stage 2
RideOnExit entry/exit
DriveEnter entry/exit
```

Screenshot result: the visible climb/boarding animation remained present at 150ms
and 500ms. This places the helper on the path without making it the sole visible
boarding gate. The current source leaves this hook installed for tracing only.

## 2026-07-02 DriveEnter Animation State Reset Experiment

Experiment: after `DriveState_OnEnter` returned, call the original animation
component state wrapper for the same component with `state=1`.

Runtime result:

```text
DriveEnter requested AnimSetState state=1 animComponent=...
DriveEnter exit ... b18B=1 b191=1 b381=0x4
```

Screenshot result: the player remained in the same climb/boarding pose at 150ms
and 500ms. This shows that a post-Drive reset to `state=1` does not replace the
earlier mount-side animation setup that has already been recorded.

The current source no longer performs this state reset.

## 2026-07-02 Direct Attach From RideOnEnter Experiment

Earlier skip-OnEnter testing simply returned from `RideOnState_OnEnter` after
setting two runtime fields. That did not crash, but it also did not call
`ProcessVehicleAttach`, so no attach/Drive path was observed.

The current direct-attach experiment changes that approach:

1. Skip the original `RideOnState_OnEnter` body.
2. Set minimal observed fields for the automated vehicle path.
3. Call the original `ProcessVehicleAttach` trampoline twice from the OnEnter hook.
4. If stage reaches `2`, write `plugin + 0x11A = 2`.

Runtime result from `capture_boarding_visual.ps1`:

```text
[20:57:30.127][VehicleBoard] RideOnEnter entry ...
[20:57:30.127][VehicleBoard] SeatTransition call caller=0x...B1CAD36 ... start=1
[20:57:30.128][VehicleBoard] ClassifyApproach result=1 ... cur=0 next=1 ... stage=0 ... b18A=1
[20:57:30.128][VehicleBoard] RideOnEnter original skipped; direct attach attempted=1 ... cur=0 next=2 ... stage=2 ... b189=1 b18A=1 b381=0x2
[20:57:30.129][VehicleBoard] RideOnExit entry ... cur=1 next=2 ... elapsed=0.0166834
[20:57:30.129][VehicleBoard] DriveEnter entry ... elapsed=0.0166834
[20:57:30.130][VehicleBoard] DriveEnter exit ... b18B=1 b191=1 b381=0x4
```

This confirms that the direct-attach OnEnter hook reaches Drive in the same frame
instead of waiting for the normal RideOn timer.

Screenshot result:

- `01_board_150ms.png`: no original long close-up climb sequence, but the player
  still has a transient offset/partial-body pose near the vehicle.
- `02_board_500ms.png`: the player is already attached near the vehicle/seat area,
  still in a transitional seated pose.
- `03_board_1200ms.png`: the player has settled into the vehicle seat.

This is the first current experiment that visibly removes the long climb sequence
and exposes a shorter pop/pose-settle window during the first several hundred
milliseconds.

Follow-up boundary check: pre-seeding `rideRuntime + 0x18B = 1` before requesting
Drive reroutes the transition to `next=3` rather than the normal Drive path:

```text
RideOnEnter original skipped; direct attach attempted=1 ... next=2 ... b18B=1
RideOnExit entry ... cur=1 next=3 ... b18B=1
```

The current source intentionally leaves `b18B` unchanged in the direct OnEnter hook;
`DriveState_OnEnter` sets it normally.

Follow-up boundary check: re-enabling the old
`ProcessVehicleAttach -> PresentationGlobal_RequestAction` suppressor in the
direct-attach build left the observed automated path and the 150ms visual artifact
unchanged. The current source does not install that suppressor.

Follow-up boundary checks for older suppressors in the direct-attach build:

- Suppressing both the RideOn entity message (`0x140F99D28 -> sub_1401618C0`) and
  Drive entry action `0x49` kept the state machine stable, but screenshots showed
  the player standing on the vehicle at 500ms. Drive action `0x49` appears useful
  for the remaining seated-pose convergence in the direct-attach path.
- Suppressing only the RideOn entity message also kept the state machine stable, but
  screenshots still showed the player standing on/near the seat at 1200ms. The
  message may participate in the remaining seat/pose convergence and is not
  suppressed in the current source.

