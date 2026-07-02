# Fast Vehicle Boarding Experiment

## Scope

This document records the currently verified fast vehicle boarding experiment for the
player RideVehicle path. It only includes observed behavior from the current trace
plugin and IDA-confirmed state fields.

## Current Source State 2026-07-02 20:57

The current `ds2_vehicle_boarding_trace` build is a direct-attach experiment. It
installs:

- `DSPlayerRideVehicleActionPlugin_Init` (`0x1410047B0`)
- `DSPlayerVehicleRideOnState_ProcessVehicleAttach` (`0x140F9A370`)
- `DSPlayerVehicleRideOnState_OnExit` (`0x140F99990`)
- `DSPlayerVehicleDriveState_OnEnter` (`0x140F8EB40`)
- `DSPlayerVehicleRideOnState_ClassifyBoardingApproach` (`0x140F9B4A0`)
- `DSPlayerVehicleRideOnState_OnEnter` (`0x140F98CE0`)
- RideOn animation-component state wrapper (`0x140DB9A10`)
- seat/controller transition helper (`0x141F6BDC0`)

It no longer installs the earlier exploratory suppressors for:

- `PresentationGlobal_RequestAction` (`0x140E21860`)
- `PresentationGlobal_WriteActionParam` (`0x140E21970`)
- `sub_140B198E0` Drive-entry action-list push
- `sub_1401618C0` RideOn entity message send
- `sub_140F9B670` approach-derived seat state write

The current behavior change is:

1. `RideOnState_OnEnter` does not call the original OnEnter body.
2. The hook initializes the minimum observed runtime fields currently needed for
   the automated vehicle path: `rideOn + 0x198 = 0`,
   `rideRuntime + 0x191 = 1`, `rideRuntime + 0x2A0 = 1`,
   and `rideRuntime + 0x3B0 = 1`.
3. The hook calls the original `ProcessVehicleAttach` trampoline twice. This is
   intended to reproduce the normal stage `0 -> 1` and `1 -> 2` progression
   without entering the original RideOn animation OnEnter body.
4. If the resulting snapshot has `stage == 2`, write
   `*(uint16_t *)(plugin + 0x11A) = 2` to request the normal Drive state.
5. The separate `ProcessVehicleAttach` hook still retains the earlier fallback:
   if a later original attach call advances to `stage == 2`, it also writes
   `*(uint16_t *)(plugin + 0x11A) = 2`.

The animation-component wrapper and seat/controller transition helper are currently
installed for tracing only. They do not suppress calls in the current source state.

This build intentionally leaves the ProcessAttach presentation request, Drive-entry
`0x49` action push, RideOn entity message, and approach-derived seat-state write
intact. The current working hypothesis is that bypassing the original RideOn OnEnter
body removes the long climb animation more effectively than suppressing individual
presentation or animation-state requests after OnEnter has already run.

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

Follow-up user visual feedback: suppressing only this
`RideOnState_OnEnter -> PresentationGlobal_WriteActionParam` call produced no
visible change to the boarding animation. This call is therefore not the animation
start point.

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
showed that suppressing only this `state=5` request does not skip the visible
boarding animation. The player was still visibly climbing/boarding at both:

- `build/boarding_capture/current/01_board_150ms.png`
- `build/boarding_capture/current/02_board_500ms.png`

This call is therefore not sufficient as the animation skip point.

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

Screenshot result: the visible climb/boarding animation still played at 150ms and
500ms. This helper is therefore not sufficient as an animation skip point. The
current source leaves this hook installed for tracing only.

## 2026-07-02 DriveEnter Animation State Reset Experiment

Experiment: after `DriveState_OnEnter` returned, call the original animation
component state wrapper for the same component with `state=1`.

Runtime result:

```text
DriveEnter requested AnimSetState state=1 animComponent=...
DriveEnter exit ... b18B=1 b191=1 b381=0x4
```

Screenshot result: the player still visibly remained in the same climb/boarding
pose at 150ms and 500ms. Resetting the wrapper to `state=1` after Drive entry is
therefore not sufficient to cancel the already-started visible boarding animation.

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

This is the first current experiment that visibly removes the long climb sequence,
but it is not yet a polished final result because the first several hundred
milliseconds still show a pop/pose-settle artifact.

Follow-up negative check: setting `rideRuntime + 0x18B = 1` before requesting Drive
is not valid. The run reached `RideOnExit` with `next=3` and did not enter the
normal Drive path:

```text
RideOnEnter original skipped; direct attach attempted=1 ... next=2 ... b18B=1
RideOnExit entry ... cur=1 next=3 ... b18B=1
```

The current source intentionally leaves `b18B` unchanged in the direct OnEnter hook;
`DriveState_OnEnter` sets it normally.

Follow-up negative check: re-enabling the old
`ProcessVehicleAttach -> PresentationGlobal_RequestAction` suppressor in the
direct-attach build did not produce a useful hit in the observed automated path
and did not improve the 150ms visual artifact. The current source does not install
that suppressor.

Follow-up negative checks for older suppressors in the direct-attach build:

- Suppressing both the RideOn entity message (`0x140F99D28 -> sub_1401618C0`) and
  Drive entry action `0x49` kept the state machine stable, but screenshots showed
  the player standing on the vehicle at 500ms. Drive action `0x49` appears useful
  for the remaining seated-pose convergence in the direct-attach path.
- Suppressing only the RideOn entity message also kept the state machine stable, but
  screenshots still showed the player standing on/near the seat at 1200ms. The
  message may participate in the remaining seat/pose convergence and is not
  suppressed in the current source.

## Historical Hook Behavior Before 2026-07-02 20:18

Earlier exploratory builds installed:

- `DSPlayerRideVehicleActionPlugin_Init` (`0x1410047B0`)
- `DSPlayerVehicleRideOnState_ProcessVehicleAttach` (`0x140F9A370`)
- `DSPlayerVehicleRideOnState_OnExit` (`0x140F99990`)
- `DSPlayerVehicleDriveState_OnEnter` (`0x140F8EB40`)
- `sub_140E21860` global presentation/action request helper
- `sub_140B198E0` byte action-list push helper
- `sub_1401618C0` entity message send helper
- `sub_140F9B670` approach-derived seat state request helper

The `ProcessVehicleAttach` hook calls the original function first. After the original
function returns, it captures the RideOn state and runtime fields. When the attach
handler has just advanced from a pre-stage-2 state to `stage == 2`, while the plugin
is still in RideOn (`current == 1`, `next == 1`) and `rideRuntime + 0x18A` is set,
the hook writes:

```cpp
*(uint16_t *)(plugin + 0x11A) = 2;
```

This requests the normal Drive state through the game's existing RideVehicle state
machine. It does not patch game bytes, edit assembly, or change registers at runtime.

The `sub_140E21860` hook suppresses only the call whose return address is
`0x140F9AFB1`, the call site immediately after `ProcessVehicleAttach` submits the
selected ride-on presentation hash. Other calls to `sub_140E21860` are forwarded to
the original function.

The `sub_140B198E0` hook suppresses only the `DriveState_OnEnter` call whose return
address is `0x140F8EEF0`, and only when the byte action value is `0x49`. Other calls
to the byte action-list helper are forwarded to the original function.

The `sub_1401618C0` hook suppresses only the `RideOnState_Update` call whose return
address is `0x140F99D28`. Other entity messages are forwarded to the original
function.

The `sub_140F9B670` hook suppresses the current RideOn seat state request after
`ClassifyApproach` has identified the seat object. The hook logs the relevant
seat-object fields and returns without writing the approach-derived request value.

## Runtime Evidence

2026-07-01 run with manual user intervention:

```text
[23:47:13.508][VehicleBoard] Init result=1 plugin=0x37E9C4F0100 snapshot=unavailable
[23:47:13.509][VehicleBoard] Attach rideOn=0x37EF1BEC5C0 stage 0->1 cur 1->1 next 1->1 b189 0->0 b18A 0->1 b18B 0->0
[23:47:13.519][VehicleBoard] Attach rideOn=0x37EF1BEC5C0 stage 1->2 cur 1->1 next 1->1 b189 0->1 b18A 1->1 b18B 0->0
[23:47:13.520][VehicleBoard] FastDrive requested after ProcessVehicleAttach stage 2
```

The same process produced repeated successful `Init -> Attach stage 0->1 -> Attach
stage 1->2 -> FastDrive requested` sequences at:

- `23:47:21`
- `23:47:37`
- `23:47:45`
- `23:47:56`
- `23:48:07`

No crash was observed in that run. The game reached `DLL_PROCESS_DETACH` normally.

2026-07-02 automated run after adding `DriveState_OnEnter` tracing:

```text
[00:04:09.934][VehicleBoard] Init result=1 plugin=0x2FB6B8F0100 snapshot=unavailable
[00:04:09.936][VehicleBoard] Attach rideOn=0x2FBC2338F80 stage 0->1 cur 1->1 next 1->1 b189 0->0 b18A 0->1 b18B 0->0 kind=1 b3B1=0 seatKey=0xE003C00000002C8
[00:04:09.951][VehicleBoard] Attach rideOn=0x2FBC2338F80 stage 1->2 cur 1->1 next 1->1 b189 0->1 b18A 1->1 b18B 0->0 kind=1 b3B1=0 seatKey=0xE003C00000002C8
[00:04:09.951][VehicleBoard] FastDrive requested after ProcessVehicleAttach stage 2
[00:04:09.966][VehicleBoard] DriveEnter entry plugin=0x2FB6B8F0100 cur=1 next=2 flag=0 seatKey=0xE003C00000002C8 rideOn=0x2FBC2338F80 runtime=0x2FB6B8F0100 kind=1 stage=2 elapsed=0.0458792 b189=1 b18A=1 b18B=0 b190=0 b191=0 b192=0 b381=0x0 b3B1=0
[00:04:09.966][VehicleBoard] DriveEnter exit plugin=0x2FB6B8F0100 cur=1 next=2 flag=0 seatKey=0xE003C00000002C8 rideOn=0x2FBC2338F80 runtime=0x2FB6B8F0100 kind=1 stage=2 elapsed=0.0458792 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x4 b3B1=0
```

The automated run completed the board, dismount, and quit sequence without crashing.
This confirms that the fast request reaches `DriveState_OnEnter`. At Drive entry,
`cur` is still `1` and `next` is `2` because the transition dispatcher has not yet
written the new current-state byte. Drive entry then sets `rideRuntime + 0x18B = 1`,
`rideRuntime + 0x191 = 1`, and `rideRuntime + 0x381 bit 0x04`.

2026-07-02 automated run after adding `RideOnState_OnExit` tracing:

```text
[00:07:44.224][VehicleBoard] FastDrive requested after ProcessVehicleAttach stage 2
[00:07:44.239][VehicleBoard] RideOnExit entry ... cur=1 next=2 ... b18B=0 b191=1 b381=0x0
[00:07:44.240][VehicleBoard] RideOnExit exit ... cur=1 next=2 ... b18B=0 b191=0 b381=0x0
[00:07:44.240][VehicleBoard] DriveEnter entry ... cur=1 next=2 ... b18B=0 b191=0 b381=0x0
[00:07:44.240][VehicleBoard] DriveEnter exit ... cur=1 next=2 ... b18B=1 b191=1 b381=0x4
```

The automated run completed board, dismount, and quit without crashing. This confirms
the fast path still executes the normal RideOn exit handler before Drive entry. The
remaining camera/presentation behavior is therefore not explained by a skipped
`RideOnState_OnExit`; it is more likely caused by presentation work started earlier
in `RideOnState_OnEnter` or `ProcessVehicleAttach`.

2026-07-02 automated run with `ProcessVehicleAttach -> sub_140E21860` suppression:

```text
[00:12:15.543][VehicleBoard] Init result=1 plugin=0x4FCE38F0100 snapshot=unavailable
[00:12:15.545][VehicleBoard] PresentationRequest suppressed request=0x53758BED mode=8 target=0x4FCCEDC1800
[00:12:15.545][VehicleBoard] Attach rideOn=0x4FD3AF29300 stage 0->1 cur 1->1 next 1->1 b189 0->0 b18A 0->1 b18B 0->0 kind=1 b3B1=0 seatKey=0xE003C00000002C8
[00:12:15.561][VehicleBoard] Attach rideOn=0x4FD3AF29300 stage 1->2 cur 1->1 next 1->1 b189 0->1 b18A 1->1 b18B 0->0 kind=1 b3B1=0 seatKey=0xE003C00000002C8
[00:12:15.562][VehicleBoard] FastDrive requested after ProcessVehicleAttach stage 2
[00:12:15.576][VehicleBoard] RideOnExit entry ... b18B=0 b191=1 b381=0x0
[00:12:15.577][VehicleBoard] RideOnExit exit ... b18B=0 b191=0 b381=0x0
[00:12:15.577][VehicleBoard] DriveEnter entry ... b18B=0 b191=0 b381=0x0
[00:12:15.577][VehicleBoard] DriveEnter exit ... b18B=1 b191=1 b381=0x4
```

The automated run completed board, dismount, and quit without crashing. This confirms
that suppressing this presentation request does not prevent the fast state transition
or Drive entry for the observed automated boarding path. The automated run does not
provide visual evidence about camera presentation.

## Visual Camera Observation

2026-07-02 user visual observation after the presentation-request suppression build:

- After boarding, the camera faces toward the vehicle front.
- The user noted this as unusual compared with the normal post-board camera, which
  appears to preserve a north/vehicle-heading-facing front orientation.

This observation suggests the suppressed `ProcessVehicleAttach -> sub_140E21860`
request may include part of the normal boarding camera/presentation orientation. The
fast path still reaches Drive, so the remaining camera state may now be coming from
Drive entry/default drive camera behavior rather than the full RideOn presentation.

2026-07-02 corrected visual observation:

The earlier split into two visible phases was a misread caused by the camera no
longer staying locked on the player. After careful visual review of the build that
suppresses both the `ProcessVehicleAttach -> sub_140E21860` request and the Drive
entry `0x49` action push, the user observed that the boarding animation still appears
to play in full.

This supersedes the earlier "phase 1 is gone" note. The current hooks change the
logged state transition and camera/presentation behavior, but they have not been
visually confirmed to skip the player boarding animation.

## Drive Entry 0x49 Candidate

Targeted IDA analysis of `DSPlayerVehicleDriveState_OnEnter` found a candidate for
the remaining sit-down/seat-settle phase:

```text
0x140F8EEAE: prepares byte value 0x49
0x140F8EEEB: calls sub_140B198E0([state+0xC0]+0x4B18, &0x49)
0x140F8EEF7: sets [state+0xC0]+0x4AEA = 1
0x140F8EF05: sets [state+0xC0]+0x4AE8 = 1
```

This path runs in Drive entry when `rideRuntime + 0x2A0` is `1` or `2`. In all
current fast-drive tests, `kind=1`, so this path is active.

2026-07-02 automated confirmation:

```text
[00:25:28.938][VehicleBoard] DriveEnter entry ... kind=1 ... b18B=0 b381=0x0
[00:25:28.939][VehicleBoard] DriveSeatActionPush action=0x49 list=0x5D52C21CB18
[00:25:28.939][VehicleBoard] DriveEnter exit ... kind=1 ... b18B=1 b381=0x4
```

The automated run completed board, dismount, and quit without crashing. This confirms
that the `0x49` action push occurs in the current fast path and is timed exactly
between Drive entry and Drive-entry completion.

2026-07-02 automated run with the `0x49` Drive entry action suppressed:

```text
[00:28:41.862][VehicleBoard] DriveEnter entry ... kind=1 ... b18B=0 b381=0x0
[00:28:41.862][VehicleBoard] DriveSeatActionPush action=0x49 list=0x28DB246CB18 suppressed
[00:28:41.862][VehicleBoard] DriveEnter exit ... kind=1 ... b18B=1 b381=0x4
```

The automated run completed board, dismount, and quit without crashing. This confirms
that skipping the `0x49` action-list push does not prevent the fast path from entering
Drive state or setting the known Drive runtime flags.

The follow-up visual review showed that suppressing this action is not sufficient to
skip the visible player boarding animation.

## RideOn Entity Message Path

Targeted IDA analysis shows a separate entity-message path in the RideOn state:

```text
0x140F9AC52: ProcessVehicleAttach sets rideRuntime + 0x189 = 1
0x140F9AC60: ProcessVehicleAttach ORs rideRuntime + 0x381 with 0x02
0x140F9AC67: ProcessVehicleAttach sets rideOn stage to 2
0x140F99C62: RideOnState_Update checks rideRuntime + 0x381 bit 0x02
0x140F99C73: RideOnState_Update clears that bit
0x140F99D23: RideOnState_Update calls sub_1401618C0 with an off_1431190A8 message
```

`sub_1401618C0` contains `EntityMessaging` diagnostics and dispatches entity
messages. `sub_1402633E0`, called in the same RideOn update block before
`sub_1401618C0`, also packages or queues entity messages and marks entity message
state. This path is distinct from the suppressed `sub_140E21860` presentation hash
request and from the Drive entry `0x49` action-list push.

2026-07-02 automated run with this entity message suppressed:

```text
[00:41:32.892][VehicleBoard] FastDrive requested after ProcessVehicleAttach stage 2
[00:41:32.908][VehicleBoard] RideOnEntityMessage suppressed queue=0x2C12C96C2D0 lock=0x2C12C96C2A8 message=0x1D2ABFF478 mode=0
[00:41:32.908][VehicleBoard] RideOnExit entry ... stage=2 ... b381=0x0
[00:41:32.908][VehicleBoard] DriveEnter entry ... stage=2 ... b381=0x0
[00:41:32.909][VehicleBoard] DriveSeatActionPush action=0x49 list=0x2C194634B18 suppressed
[00:41:32.909][VehicleBoard] DriveEnter exit ... b18B=1 b191=1 b381=0x4
```

The automated board, dismount, and quit flow completed without crashing. This
confirms that the current build actually suppresses the RideOn entity-message
dispatch and still reaches Drive. It does not, by itself, prove that the visible
player boarding animation is skipped.

2026-07-02 follow-up run with message fields logged:

```text
[00:43:52.153][VehicleBoard] FastDrive requested after ProcessVehicleAttach stage 2
[00:43:52.168][VehicleBoard] RideOnEntityMessage suppressed ... mode=0 vtbl=0x7FF6995490A8 b8=0 word10=0x100 b12=1 payload=0x0
[00:43:52.168][VehicleBoard] RideOnExit entry ... stage=2 ... b381=0x0
[00:43:52.169][VehicleBoard] DriveEnter entry ... stage=2 ... b381=0x0
[00:43:52.169][VehicleBoard] DriveEnter exit ... b18B=1 b191=1 b381=0x4
```

The runtime vtable corresponds to the rebased `off_1431190A8` message used at
`0x140F99D23`. The fixed message fields confirm that the hook is suppressing one
specific RideOn entity-message type, not an arbitrary generic dispatch.

## Seat State Request Path

`ProcessVehicleAttach` calls `sub_140F9B670` immediately after
`DSPlayerVehicleRideOnState_ClassifyBoardingApproach` and before the
`sub_140E21860` presentation request. Static analysis shows that this helper writes
approach-derived values into the resolved seat/vehicle object:

```text
0x140F9B7EE: writes seatObject + 0x1314 = 3 or 4 for approach 2
0x140F9B81D: writes seatObject + 0x1314 = 5 for approach 0
0x140F9B839: writes seatObject + 0x1314 = 6 for approach 1
0x140F9B95F: alternate object-family path writes seatObject + 0x125C
```

2026-07-02 automated trace with previous suppressions still active:

```text
[00:47:29.194][VehicleBoard] ClassifyApproach result=0 seatObject=0x4D7FA1D0000 ...
[00:47:29.194][VehicleBoard] SeatStateUpdate approach=0 b3B1=0 seatObject=0x4D7FA1D0000 p12F8=0x4D80A405B00->0x4D80A405B00 v125C=1->1 v1310=0->0 v1314=4294967295->5 result=1
[00:47:29.194][VehicleBoard] PresentationRequest suppressed request=0x53758BED mode=8 target=0x4D7FA1D0000
```

This confirms that even after suppressing the presentation hash, the RideOn entity
message, and the Drive entry `0x49` action, the attach path still submits an
approach-specific seat state request by writing `seatObject + 0x1314`.

2026-07-02 automated run with `sub_140F9B670` suppressed:

```text
[00:49:30.205][VehicleBoard] ClassifyApproach result=0 seatObject=0x44E2C9B0000 ...
[00:49:30.205][VehicleBoard] SeatStateUpdate suppressed approach=0 b3B1=0 seatObject=0x44E2C9B0000 p12F8=0x44E40405B00->0x44E40405B00 v125C=1->1 v1310=0->0 v1314=4294967295->4294967295 result=1
[00:49:30.205][VehicleBoard] PresentationRequest suppressed request=0x53758BED mode=8 target=0x44E2C9B0000
[00:49:30.220][VehicleBoard] FastDrive requested after ProcessVehicleAttach stage 2
[00:49:30.235][VehicleBoard] RideOnEntityMessage suppressed ... word10=0x100 b12=1 payload=0x0
[00:49:30.236][VehicleBoard] DriveEnter exit ... b18B=1 b191=1 b381=0x4
```

This confirms that the current build skips the approach-derived seat state request
that previously wrote `seatObject + 0x1314 = 5`, while still completing the automated
board, dismount, and quit flow.

## Presentation Request Functions

Targeted IDA analysis of the two global presentation/action helpers:

- `sub_140E21860`: writes a global request id/hash at `qword_14623E908 + 0xE8`,
  sets byte `+0x1E`, stores a mode at `+0xF0`, and updates a referenced target object
  pointer at `+0xC8`. `ProcessVehicleAttach` calls this helper at `0x140F9AFAC`
  after selecting a ride-on presentation hash.
- `sub_140E21970`: writes an integer parameter at `qword_14623E908 + 0xEC`, sets
  byte `+0x1F`, and writes a float parameter at `+0x158`.

This strengthens the current explanation for the remaining camera/presentation:
the fast path preserves the normal state transition, but `ProcessVehicleAttach` has
already submitted a ride-on presentation/action request before the fast Drive request.

## Added Trace Fields

The current trace build also logs additional fields for the next validation pass:

- `seatKey`: value from `plugin + 0x220`
- `kind`: value from `rideRuntime + 0x2A0`
- `b3B1`: byte from `rideRuntime + 0x3B1`
- `ClassifyApproach result`: return value from
  `DSPlayerVehicleRideOnState_ClassifyBoardingApproach` (`0x140F9B4A0`)

These fields are included because `ProcessVehicleAttach` and `DriveState_OnEnter`
branch on runtime/seat-related data near these offsets. They are intended to help
correlate the front, driver-side, and passenger-side boarding angles with runtime
state. No angle mapping has been confirmed yet.

2026-07-02 automated path:

```text
ClassifyApproach result=0
kind=1
b3B1=0
seatKey=0xE003C00000002C8
PresentationRequest suppressed request=0x53758BED mode=8
```

This maps the current automated board path to approach classification `0` and
presentation request `0x53758BED`. The exact real-world angle label still depends on
visual confirmation, but it is the same path used in the current fast-drive tests.

2026-07-02 manual three-angle run:

The user manually triggered three boarding approaches and dismounted after each. The
log shows two repeated cycles with the same classification/request order:

| Time | ClassifyApproach | Presentation request | Common fields |
|------|------------------|----------------------|---------------|
| `00:19:28` | `0` | `0x53758BED` | `kind=1`, `b3B1=0`, `seatKey=0xE003C00000002C8` |
| `00:19:37` | `2` | `0x3897A3D5` | `kind=1`, `b3B1=0`, `seatKey=0xE003C00000002C8` |
| `00:19:45` | `1` | `0x6F53F3A5` | `kind=1`, `b3B1=0`, `seatKey=0xE003C00000002C8` |
| `00:19:54` | `0` | `0x53758BED` | `kind=1`, `b3B1=0`, `seatKey=0xE003C00000002C8` |
| `00:20:02` | `2` | `0x3897A3D5` | `kind=1`, `b3B1=0`, `seatKey=0xE003C00000002C8` |
| `00:20:10` | `1` | `0x6F53F3A5` | `kind=1`, `b3B1=0`, `seatKey=0xE003C00000002C8` |

Each attempt still followed the fast path:

```text
Attach stage 0->1
Attach stage 1->2
FastDrive requested after ProcessVehicleAttach stage 2
RideOnExit entry/exit
DriveEnter entry/exit
```

Assuming the user triggered the approaches in the order previously described
(`front`, `driver-side`, `passenger-side`), the current inferred mapping is:

| User-facing approach | ClassifyApproach | Presentation request |
|----------------------|------------------|----------------------|
| Front of vehicle | `0` | `0x53758BED` |
| Driver-side | `2` | `0x3897A3D5` |
| Passenger-side | `1` | `0x6F53F3A5` |

This mapping is derived from the user's manual trigger order plus the repeated log
sequence. If a later controlled run uses a different trigger order, update this table.

## Visual Observation

During the same run, the user manually observed that the front-of-vehicle boarding
angle moved the player to the seat immediately. A vehicle camera/animation presentation
still remained, but the long intermediate movement state was skipped for that angle.

The user described three boarding-angle categories:

- front of vehicle
- driver-side
- passenger-side

Only the front-of-vehicle behavior above is recorded here as observed for the current
experiment.

## Interpretation

The result supports the current model:

- `DSPlayerVehicleRideOnState_ProcessVehicleAttach` performs the player-specific
  attach preparation before the long wait completes.
- The normal wait path later uses `RideOnState_Update` to request Drive by writing
  `plugin + 0x11A = 2`.
- Requesting Drive immediately after `ProcessVehicleAttach` reaches stage 2 can
  shorten at least one player boarding path while preserving the player RideVehicle
  attach path.

The remaining camera presentation means this experiment has not fully removed all
boarding presentation logic.

## Drive Entry Follow-Up

Static IDA analysis of `DSPlayerVehicleDriveState_OnEnter` (`0x140F8EB40`) explains
why requesting Drive before the normal RideOn timer finishes can still work:

- `0x140F8EDB9`: Drive entry can set `rideRuntime + 0x18A = 1` if that byte was not
  already set, after `sub_141F6BDC0` succeeds.
- `0x140F8EE74`: Drive entry can set `rideRuntime + 0x18B = 1` if that byte was not
  already set, after updating the resolved seat object's protected flags.
- `0x140F8F11B`: Drive entry sets `rideRuntime + 0x381 bit 0x04`, matching earlier
  runtime polling where Drive state showed `b381 = 0x4`.

This means the fast path does not need to wait for the normal RideOn update loop to
set `rideRuntime + 0x18B`; the Drive entry path has its own fallback for that field.

## Hook Implementation Note

`DriveState_OnEnter` starts with `push rdi; push r14; sub rsp, 68h; mov rax,
[rcx+90h]`. The trampoline must copy all 15 bytes of this prologue. The trampoline
jump-back must also preserve `rax`, because the copied prologue initializes `rax`
for the next original instruction. The shared `JumpHook::MakeTrampoline` now uses a
register-preserving absolute return jump for this reason.

`RideOnState_OnExit` starts with `sub rsp, 48h; mov rax, [rcx+190h]; mov
[rsp+40h], rdi; mov rdi, rcx`. The current hook copies all 19 bytes before jumping
back to `0x140F999A3`.
