# Fast Vehicle Boarding Experiment - Message, Seat, and Presentation Traces

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
confirmed that the experimental build suppressed the RideOn entity-message
dispatch and still reached Drive. It does not, by itself, prove that the visible
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

This confirmed that the experimental build skipped the approach-derived seat state
request that previously wrote `seatObject + 0x1314 = 5`, while still completing the
automated board, dismount, and quit flow.

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

