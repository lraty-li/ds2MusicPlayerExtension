# Fast Vehicle Boarding Experiment - Runtime Evidence and Drive Entry

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
logged state transition and camera/presentation behavior, while the reviewed visual
run still retained the visible boarding animation.

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

The follow-up visual review showed that this action can be isolated while the
visible player boarding animation remains present.

