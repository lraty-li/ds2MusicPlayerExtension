# Fast Vehicle Boarding Animation System Analysis

## Scope

This note records verified facts about the player RideOn animation stack for the
fast vehicle boarding project. It is based on targeted IDA MCP inspection of known
functions and the runtime observations already recorded in the project logs and
RE notes. No global search was used.

The purpose is to separate the systems that were previously grouped together as
"boarding animation":

- RideOn OnEnter parameter envelope
- animation component wrapper and inner track rebuild
- player attach and approach-specific seat state
- presentation/action request
- RideOn Update seat pose request
- RideOn Update action slot filters

## Key Function Names

Current IDA names relevant to this note:

| Address | Name | Role |
|---|---|---|
| `0x140F98D00` | `DSPlayerVehicleRideOnState_OnEnter` | Initializes RideOn runtime state and per-state action/animation parameters. |
| `0x140DB9A10` | `sub_140DB9A10` | Thin animation component state wrapper; forwards to inner vtable `+0x168`. |
| `0x140DBA820` | `sub_140DBA820` | Thin animation component float setter; writes `inner+0x544`. |
| `0x140EF6A20` | `AnimInner_SetStateAndEvaluateTracks` | Inner animation state evaluator reached through `0x140DB9A10`. |
| `0x140EF6040` | `AnimInner_RebuildTrackSlots` | Rebuilds active track/clip slots for the inner animation object. |
| `0x140EF5E90` | `AnimTrackSlot_SelectClipByType` | Selects a clip by type id and writes one track slot. |
| `0x140F9A390` | `DSPlayerVehicleRideOnState_ProcessVehicleAttach` | Player RideOn attach state machine. |
| `0x140F9B690` | `RideOnSeatState_ApplyApproachState` | Writes approach-derived seat state to the resolved seat object. |
| `0x140E21880` | `DSCutInCameraRequestBroker_QueueAction` | Queues a CutIn action hash and vehicle target. |
| `0x140F9B090` | `RideOnState_UpdateSeatPoseRequest` | Builds RideOn seat pose request during Update when attach stage is 2. |
| `0x1410139C0` | `RideRuntime_SetRideOnActionSlotFilters` | Applies RideOn action slot filters. |
| `0x140DB99A0` | `DSPlayerMoverAccessor_OnPreModifyAnimatedPose` | Forwards pre-modify pose processing to mover slot 49. |
| `0x140DB99C0` | `DSPlayerMoverAccessor_OnModifyAnimatedPose` | Forwards modified pose application to mover slot 50. |
| `0x140EC81C0` | `DSPlayerMover_OnPreModifyAnimatedPose` | Updates the pose-motion cache. |
| `0x140EC81E0` | `DSPlayerMover_OnModifyAnimatedPose` | Applies animation-driven player movement. |
| `0x140ED2EA0` | `DSPlayerMover_UpdatePoseMotionCache` | Builds and commits the cached pose-motion transform. |
| `0x140ECFCD0` | `DSPlayerMover_ApplyPoseMotionTransform` | One native branch that writes the player Entity world transform. |

## RideOn OnEnter Layer

`DSPlayerVehicleRideOnState_OnEnter` first resets RideOn runtime state:

- `state+0x198 = 0`
- `state+0x180 = 0`
- `rideRuntime+0x3B0 = 0`
- `rideRuntime+0x37B = 1`
- `rideRuntime+0x381 &= ~0x02`

It also sets `state+0x19C` for boarding contexts where
`[state+0xA8]+0x3970` is `3` or `9`. For context `9`, it additionally sets
`rideRuntime+0x190 = 1`. This byte is only one part of the later normal-completion
gate and is not by itself the Drive trigger.

The function then writes a broad action/animation parameter envelope into the
object at `rideOn+0x98`. The common pattern is a validity/update flag byte followed
by a value at `flag+0x10`.

Verified examples:

| Flag offset | Value offset | OnEnter value/effect |
|---|---:|---|
| `+0x3940` | `+0x3950` | computed RideOn scalar from `xmm10` |
| `+0x34C0` | `+0x34D0` | `0` |
| `+0x3970` | `+0x3980` | `0` |
| `+0x3700` | `+0x3710` | `0` |
| `+0x3730` | `+0x3740` | `-1.0` |
| `+0x3760` | `+0x3770` | value derived from `rideRuntime+0x240` |
| `+0x3790` | `+0x37A0` | `0` |
| `+0x3820` | `+0x3830` | `0` |
| `+0x3880` | `+0x3890` | `0` |
| `+0x3DC0` | `+0x3DD0` | `1.0` |
| `+0x53B0` | `+0x53C0` | `0` |

The later flag-only block marks these parameter flags active before requesting the
animation state:

`+0x1030`, `+0x1060`, `+0x1090`, `+0x10C0`, `+0x10F0`, `+0x1120`,
`+0x1150`, `+0x1180`, `+0x11B0`, `+0x11E0`, `+0x1210`, `+0x1510`.

Most are written as `(flag & 0xED) | 0x04`. The `+0x1120` flag also derives an
extra `0x10` bit from `rideRuntime+0x372`.

## Animation Component Layer

Immediately after the parameter envelope, `OnEnter` calls the animation component:

```text
0x140F9987A: animComponent->vtable+0x250(xmm1 = 0)
0x140F9988F: animComponent->vtable+0x20(state = 5)
```

At runtime the `+0x250` slot resolves to the wrapper at `0x140DBA820`. This wrapper
reads `inner = [animComponent+0x8]` and writes `xmm1` to `inner+0x544`.

At runtime the `+0x20` slot resolves to the wrapper at `0x140DB9A10`. This wrapper
reads `inner = [animComponent+0x8]` and tail-jumps through `inner->vtable+0x168`.

The current vtable target for `inner->vtable+0x168` is
`AnimInner_SetStateAndEvaluateTracks` (`0x140EF6A20`). Its visible state selector is
`r8b`; it also uses the incoming `xmm1` value. For state `5`, control enters the
block at `0x140EF6A98`, checks `inner+0x54`, and calls
`AnimInner_RebuildTrackSlots`.

This confirms that `AnimSetState(5)` is not a simple enum write. It can cause the
inner animation object to rebuild and evaluate a multi-track clip set.

## Inner Track Rebuild Layer

`AnimInner_RebuildTrackSlots` (`0x140EF6040`) operates on the object at
`inner+0x28`.

When reset is enabled, it clears and resets fields in the approximate range
`+0x3B0` through `+0x510`, including clip handles, slot active bytes, timing
fields, and weights.

It then calls `AnimTrackSlot_SelectClipByType` for fixed clip type ids:

| Clip type id | Output slot | Clip handle target |
|---:|---:|---:|
| `5` | `inner+0x28+0x458` | `inner+0x28+0x3C8` |
| `4` | `inner+0x28+0x478` | `inner+0x28+0x3C0` |
| `7` | `inner+0x28+0x498` | `inner+0x28+0x3D8` |
| `6` | `inner+0x28+0x4B8` | `inner+0x28+0x3D0` |
| `9` | `inner+0x28+0x4D8` | `inner+0x28+0x3E0` |
| `8` | `inner+0x28+0x4F8` | `inner+0x28+0x3E8` |

`AnimTrackSlot_SelectClipByType` searches the action tree slot for the requested
clip type id. When the selected clip handle changes, it writes:

- `slot+0x00`: clip count/index field
- `slot+0x04`: current time/value from the selected clip helper
- `slot+0x0C`: default rate/weight selected from clip subtype
- `slot+0x18`: active flag `1`
- clip handle target: selected clip handle

This layer depends on the current action tree content. The seated/mount pose is
therefore data-driven by available clip slots and the parameter envelope, not by a
single hard-coded RideOn pose enum.

## Attach, Seat State, And Presentation Layer

`DSPlayerVehicleRideOnState_ProcessVehicleAttach` performs the player attach path.
The verified attach call is:

```text
0x140F9AD9D: Entity_AttachToParentAndNotify(player, vehicle, 0)
```

After attach, the function calls:

```text
0x140F9B4C0: DSPlayerVehicleRideOnState_ClassifyBoardingApproach
0x140F9B690: RideOnSeatState_ApplyApproachState
0x140E21880: DSCutInCameraRequestBroker_QueueAction
```

`RideOnSeatState_ApplyApproachState` writes approach-derived state
to the resolved seat object. Main-object writes are to `seatObject+0x1314`;
alternate object-family writes are to `seatObject+0x125C`.

Observed main-object values:

- approach `2`: writes `3` or `4`, depending on `vehicle/runtime+0x3B1` and vehicle
  byte `+0x3B1` related checks
- approach `0`: writes `5`
- approach `1`: writes `6`

The CutIn request selects a hash based on `rideRuntime+0x2A0`, approach,
`rideRuntime+0x3B1`, and seat object flags. Runtime observations mapped one
automated path to:

- `ClassifyApproach result=0`
- `kind=1`
- `b3B1=0`
- CutIn action request `0x53758BED`

Manual three-angle observations recorded request hashes:

| ClassifyApproach | CutIn action request |
|---:|---:|
| `0` | `0x53758BED` |
| `1` | `0x6F53F3A5` |
| `2` | `0x3897A3D5` |

This CutIn layer is separate from the `rideOn+0x98` parameter envelope and
from the inner track rebuild. Suppressing a CutIn request does not reproduce
the missing pose setup from a skipped `OnEnter`.

## Update-Side Pose And Filter Layer

`DSPlayerVehicleRideOnState_Update` only enters the completion-relevant block when
`state+0x198 == 2`.

Before the vanilla normal completion write, the Update function calls:

```text
0x140F99E10: RideOnState_UpdateSeatPoseRequest
0x140F99E1E: RideRuntime_SetRideOnActionSlotFilters
```

`RideOnState_UpdateSeatPoseRequest` resolves the current seat/action object and
chooses a RideOn seat pose id. It counts seat action clip types `93`, `92`, `97`,
and `98`, and also checks `SeatAction_HasProgressGate`.

Verified pose id families include:

- stage/action branch 1: `24`, `27`, `30`, `33`, `36`
- stage/action branch 2: `28`, `31`, `34`, `37`
- branch 3: `26`
- default initial value: `25`

It writes the pose request into the owner block:

- `+0x788`: selected pose id
- `+0x7BC`: companion value `15`
- `+0x2104`: pose request active byte `1`
- `+0x2154`: dirty bit `0x1000`

`RideRuntime_SetRideOnActionSlotFilters` toggles RideOn action slots
`4`, `5`, `6`, `7`, `9`, `15`, and `17`, and records applied state at
`runtime+0x24596`.

## Current Boundary From Verified Facts

The player fast-boarding intervention must preserve these verified setup layers:

1. `DSPlayerVehicleRideOnState_OnEnter` parameter envelope on `rideOn+0x98`.
2. Animation component `+0x250` phase reset to `inner+0x544`.
3. Animation component `+0x20` state `5`, which reaches inner track evaluation and
   can rebuild clip slots.
4. `ProcessVehicleAttach` player attach through `Entity_AttachToParentAndNotify`.
5. `RideOnState_UpdateSeatPoseRequest` after attach stage `2`.
6. `RideRuntime_SetRideOnActionSlotFilters` after attach stage `2`.

Later 2026-07-18 analysis closed the two independent long-running presentation
layers:

- the visible player boarding descriptor is evaluated by the fullgame ActionGraph
  evaluator and reaches the RideOn `0xED` event through normal result evaluation;
- the camera presentation is a `DSCutInCamera` action with its own
  elapsed/duration, finished flag, CameraMode removal, and Deactivate cleanup.

The current verified boundary is therefore more specific: preserve OnEnter,
ProcessVehicleAttach, the stage-2 seat pose/filter pass, and the original
ActionGraph result construction; accelerate the active descriptor through its
normal evaluator; let the native RideOn completion block enter Drive; wait for the
native modified-pose submission; then finish CutIn through its original playback
and Deactivate lifecycle.

## Modified-pose submission and world basis

The logical ABI of both `DSPlayerMoverAccessor` pose slots is:

```cpp
void Method(
    DSPlayerMoverAccessor* self,
    float frameDelta,
    AnimatedPoseWrapper* wrapper);
```

`MsgPreModifyAnimatedPose` reaches accessor slot 0 and mover slot 49. This path
updates the cached pose-motion transform but did not change the player Entity
basis in the verified boarding run.

`MsgModifyAnimatedPose` reaches accessor slot 1 and mover slot 50. Runtime
before/after sampling proves that the player Entity world basis changes inside
this original call. Static analysis confirms that mover slot 50 submits the
animation-driven player transform before it returns.

For the verified direct-front boarding action, the descriptor end left the player
with `M33≈0.5708`. Drive Enter itself preserved that basis. The next Drive-frame
slot 1 call changed it to `M33≈0.9998`, establishing the native driving pose before
the CutIn camera handoff.

`DSCutInCamera_Deactivate` for flags `0x40A14A0C` reads this current world basis
from `DSPlayerEntity+0x100` and uses it to calculate the ordinary-camera handoff.
This verified consumer explains why the player-pose submission and camera
Deactivate order are part of the same fast-boarding correctness boundary.
