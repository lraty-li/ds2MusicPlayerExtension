# Fast Vehicle Boarding Experiment

## Scope

This document records the currently verified fast vehicle boarding experiment for the
player RideVehicle path. It only includes observed behavior from the current trace
plugin and IDA-confirmed state fields.

## Current Hook Behavior

The trace plugin hooks:

- `DSPlayerRideVehicleActionPlugin_Init` (`0x1410047B0`)
- `DSPlayerVehicleRideOnState_ProcessVehicleAttach` (`0x140F9A370`)
- `DSPlayerVehicleRideOnState_OnExit` (`0x140F99990`)
- `DSPlayerVehicleDriveState_OnEnter` (`0x140F8EB40`)
- `sub_140E21860` global presentation/action request helper

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
