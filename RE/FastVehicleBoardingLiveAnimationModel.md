# Fast Vehicle Boarding Live Animation Model

## Scope

This note records the current-version live animation model for player vehicle
boarding. It is based on targeted IDA MCP inspection of known current addresses,
runtime log review from the 2026-07-07 manual boarding flow, and IDA comments
added at the verified call sites. No global search was used.

The purpose is to explain why a fast-boarding mod cannot be treated as only a
state-machine skip or only a passenger-cargo mount reuse.

## Current Address Map

| Address | Name | Verified role |
|---|---|---|
| `0x140F98D00` | `DSPlayerVehicleRideOnState_OnEnter` | Writes the RideOn action/animation parameter envelope. |
| `0x140DB99E0` | `AnimComponent_SetPhaseFloat544_Wrapper` | Reads `inner=[component+0x8]` and writes the live phase/blend float at `inner+0x544`. |
| `0x140DB9A50` | `AnimComponent_RequestState_Wrapper` | Reads `inner=[component+0x8]` and jumps through `inner->vtable+0x170`. |
| `0x140EF6060` | `AnimInner_SetStateAndEvaluateTracks` | Evaluates the requested animation state and rebuilds track slots from the action tree. |
| `0x140EF5EB0` | `AnimInner_RebuildTrackSlots` | Selects one clip type and writes the corresponding track slot. |
| `0x140DBA840` | `AnimComponent_SetPhaseFloat544_Direct` | Runtime-observed RideOn anim component `+0x250` target; directly writes XMM1 to `inner+0x544`. |
| `0x140DB9A30` | `AnimComponent_InvokeInnerState168_Wrapper` | Runtime-observed RideOn anim component `+0x20` target; jumps to inner vtable `+0x168`. |
| `0x140EC80C0` | `AnimInner_RecordState2E0_ThenDispatch1E8` | Runtime-observed inner `+0x168` target; writes state to `inner+0x2E0`, then dispatches through inner `+0x1E8`. |
| `0x140EC80E0` | `AnimInner_SetPendingState2E8` | Runtime-observed inner `+0x170` target; marks `inner+0x2E4` and writes `inner+0x2E8`, but current RideOn `+0x20` does not dispatch here. |
| `0x140F99C60` | `DSPlayerVehicleRideOnState_Update` | Runs the stage-2 pose/filter pass and vanilla Drive completion gate. |
| `0x140F9A390` | `DSPlayerVehicleRideOnState_ProcessVehicleAttach` | Player attach and late RideOn completion state machine. |
| `0x141F6BDE0` | `SeatController_StartOrUpdateTransition` | Starts or finishes the seat controller transition with callback ownership. |
| `0x140F8EAA0` | `SeatTransition_PreAttachHelper` | Sets a low-bit pre-attach flag on the seat object. |
| `0x140F8E8F0` | `SeatTransition_StartHelper` | Sets transition bits on the seat object before `runtime+0x18B` is marked. |
| `0x140F9B090` | `RideOnState_UpdateSeatPoseRequest` | Converts current seat/action state into a RideOn seated pose request. |
| `0x1410139C0` | `RideRuntime_SetRideOnActionSlotFilters` | Applies RideOn action slot filters and records the applied state. |

## OnEnter Animation Setup

`DSPlayerVehicleRideOnState_OnEnter` still performs the mount-side setup that was
missing from earlier direct-attach experiments:

- resets RideOn stage and elapsed fields,
- writes the large action/animation parameter envelope at the block referenced by
  `rideOn+0x98`,
- marks many parameter flags active,
- calls animation component vtable `+0x250`; the current RideOn runtime object
  resolves this to `AnimComponent_SetPhaseFloat544_Direct`,
- calls animation component vtable `+0x20` with state `5`; the current RideOn
  runtime object resolves this to `AnimComponent_InvokeInnerState168_Wrapper`.

The state request is not sufficient by itself. The state request depends on the
parameter envelope and the phase/blend input written immediately before it.

## Track Evaluation Layer

Static inspection previously identified a wrapper at `0x140DB9A50` dispatching
through inner vtable `+0x170` to `AnimInner_SetStateAndEvaluateTracks`. Runtime
vtable logging corrected that for the actual current RideOn object:

```text
RideOnAnimTargets ... fn20=0x...DD299A30 fn250=0x...DD29A840
innerFn168=0x...DD3A80C0 innerFn170=0x...DD3A80E0 innerFn1E8=0x...DC5810E0
AnimEval entry ... state=5 ... v2E0=1 ... f544=0
AnimEval exit  ... state=5 ... v2E0=5 ... f544=0
```

Converted to current IDA addresses, the active RideOn path is:

```text
0x140DBA840  component +0x250 -> write inner+0x544
0x140DB9A30  component +0x20  -> inner vtable +0x168
0x140EC80C0  inner +0x168     -> write inner+0x2E0 = state
0x1400A10E0  inner +0x1E8     -> no-op/monitor stub for this runtime object
```

Therefore the current player RideOn `state=5` request is verified as a state
recording operation on `inner+0x2E0`, not as a direct call into
`AnimInner_SetStateAndEvaluateTracks`.

`AnimInner_SetStateAndEvaluateTracks` still exists in the binary, but a targeted
runtime hook on `0x140EF6060` did not receive the validated RideOn `state=5`
request. It is not the active dispatch target for the current RideOn object.

The evaluator performs live track maintenance:

- checks whether the current action tree clip handle set changed,
- clears old handles and timing fields when the track set is stale,
- reads action slots under shared locks,
- calls `AnimInner_RebuildTrackSlots` for clip types `5`, `4`, `7`, `6`, `9`, and
  `8`,
- writes per-slot clip handles, clip count/index fields, active flags, time, and
  default weight/rate values,
- advances small transition timers using the incoming frame delta/phase value.

The visible boarding animation is still data-driven live evaluation, but the
validated `state=5` write alone is not a clip cancellation point and is not
equivalent to a single hard-coded seated pose enum.

## Seat Pose And Filter Layer

After player attach reaches stage `2`, `DSPlayerVehicleRideOnState_Update` calls:

```text
RideOnState_UpdateSeatPoseRequest
RideRuntime_SetRideOnActionSlotFilters
```

`RideOnState_UpdateSeatPoseRequest` reads the current seat controller and counts
seat action clip types `93`, `92`, `97`, and `98`. It chooses RideOn pose ids in
the `24..37` family and writes:

- selected pose id into the pose/request owner block,
- companion value `15`,
- request-active byte,
- dirty bit for the pose/action owner.

`RideRuntime_SetRideOnActionSlotFilters` applies action slot filters for slots
`4`, `5`, `6`, `7`, `15`, and `17`, plus related player action filter state for
slots `9` and `15`. It records the applied state at `runtime+0x24596`.

These two calls prepare the seated pose/action context. They do not by themselves
mean the visible boarding transition has completed.

## Attach And Late Completion Gate

`DSPlayerVehicleRideOnState_ProcessVehicleAttach` still has three meaningful
phases:

1. stage `0` starts the seat transition path and moves toward attach readiness,
2. stage `1` attaches the player entity to the resolved seat object and writes
   approach/presentation state,
3. stage `2` waits for a later live-action condition before marking completion.

The late completion marker is:

```text
runtime+0x18B = 1
```

Current static inspection shows this write occurs only when the stage-2 branch
passes all of these checks:

- `rideOn+0x198 == 2`,
- `rideOn+0x19C == 0`,
- `runtime+0x18B == 0`,
- the owner/action flag at `rideOn+0xA0+0x7378` has bit 24 set.

When the gate passes, `ProcessVehicleAttach` calls `SeatTransition_StartHelper`
and then writes `runtime+0x18B = 1`.

The 2026-07-07 manual run requested Drive at:

```text
stage=2 elapsed=0.025025 b189=1 b18A=1 b18B=0 b191=1
```

That confirms the current hook preserves the Update-side pose/filter pass, but it
requests Drive before the stage-2 live-action completion marker is set.

## Practical Intervention Boundary

The reasonable intervention boundary is inside the player RideOn path, after the
OnEnter parameter/track setup and after the Update-side pose/filter pass, but it
must respect the stage-2 completion gate represented by `runtime+0x18B`.

The boundary is not:

- passenger cargo `MountableComponent_StartMount`,
- generic `Entity_AttachToParentAndNotify` alone,
- animation state `5` alone,
- immediate `plugin+0x11A = 2` on the first stage-2 frame.

The animation process indicates that the fast path should preserve live track
evaluation and seat pose/filter setup, then shorten or satisfy the specific
stage-2 completion condition that normally leads to `runtime+0x18B = 1` before
Drive is requested.

## 2026-07-07 Gate Intervention Test

An implementation test moved the intervention from `RideOnState_Update` to
`DSPlayerVehicleRideOnState_ProcessVehicleAttach`.

The hook resolves `ProcessVehicleAttach` by this current-version signature:

```text
4C 8B DC 55 56 49 8D AB ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B 81
```

The effective intervention is:

1. Let original `OnEnter` run.
2. Let original Update reach stage `2` and run pose/filter setup.
3. At `ProcessVehicleAttach` entry, only when the snapshot is
   `cur=1,next=1,stage=2,b18A=1,b18B=0,b191=1`, set
   `*(uint32_t *)(rideOn+0xA0+0x7378) |= 0x01000000`.
4. Call original `ProcessVehicleAttach`.
5. Let original `ProcessVehicleAttach` set `runtime+0x18B = 1`.
6. Let the Update hook request Drive only after `b18B=1`.

Runtime validation from `test_boarding.ps1` with `Load (17s)`:

```text
RideOnUpdate waiting for live completion gate ... stage=2 elapsed=0.025025 b18B=0
ProcessAttach gate forced=1 owner7378=0x0->0x1000000 b18B 0->1 ... elapsed=0.025025
FastDrive requested after RideOnUpdate pose/filter pass ... elapsed=0.0333667 b18B=1
```

This is the first verified fast path that uses the player RideOn live-animation
completion mechanism instead of bypassing it. It preserves the parameter envelope,
track evaluation, pose request, action slot filters, and the original
`ProcessVehicleAttach -> SeatTransition_StartHelper -> runtime+0x18B` completion
write.

## 2026-07-07 Presentation Suppression And State Dispatch

A targeted suppression test returned early from
`PresentationRequest_SetActionTarget` only for action hash `0x53758BED`.

Validated log:

```text
PresentationRequest ... action=0x53758bed a4=8 ... suppressed=1
ProcessAttach gate forced=1 ... b18B 0->1
FastDrive requested ... elapsed=0.0375375
DriveEnter entry
DriveEnter exit ... b381=0x4
```

The game did not crash, and the fast Drive path still completed. This proves the
global presentation request for `0x53758BED` is not required for the attach/Drive
state machine to complete under the current hook.

The same validated run logged the actual animation state dispatch immediately
before the presentation request:

```text
AnimEval entry ... inner=0x... state=5 ... v2E0=1 ... f544=0
AnimEval exit  ... inner=0x... state=5 ... v2E0=5 ... f544=0
PresentationRequest ... action=0x53758bed ... suppressed=1
```

That places the verified state `5` write before the presentation request and
before the stage-2 attach completion gate. Suppressing only `0x53758BED` can
therefore leave the visible boarding animation unchanged because the current
RideOn animation/action state has already been recorded in the animation inner
object.

## 2026-07-07 Post-Drive State Reset Experiment

An implementation test called the same runtime RideOn anim component `+0x20`
function after original `DriveEnter` returned, passing state `1`.

This does not patch bytes or modify registers. It calls the runtime object's own
virtual function:

```text
anim = rideOn+0xB0
fn20 = [anim vtable +0x20] = AnimComponent_InvokeInnerState168_Wrapper
fn20(anim, 1)
```

Validated log:

```text
DriveEnter exit ... b381=0x4
AnimEval entry ... state=1 ... v2E0=5
AnimEval exit  ... state=1 ... v2E0=1
PostDriveAnimState state=1 called=1 ...
```

The game did not crash. The experiment proves that after the fast Drive request,
the same RideOn inner animation object can be moved from the recorded boarding
state `5` back to state `1` through the original virtual dispatch path. This is a
runtime state-machine intervention, not a presentation suppression and not a
direct write to the `inner+0x2E0` field.

## Current Signatures

Useful current signatures generated by IDA MCP:

| Function | Signature |
|---|---|
| `AnimComponent_SetPhaseFloat544_Wrapper` | `48 83 EC ? 4C 8B 41 ? 4D 85 C0 74 ? E8` |
| `AnimComponent_RequestState_Wrapper` | `48 8B 49 ? 48 85 C9 74 ? 48 8B 01 48 FF A0 70 01 00 00 C3` |
| `AnimComponent_SetPhaseFloat544_Direct` | `48 8B 41 ? 48 85 C0 74 ? C5 F2 11 88 ? ? ? ? C3` |
| `AnimComponent_InvokeInnerState168_Wrapper` | `48 8B 49 ? 48 85 C9 74 ? 48 8B 01 48 FF A0 68 01 00 00 C3` |
| `AnimInner_RecordState2E0_ThenDispatch1E8` | `48 8B 01 44 8B C2 89 91 ? ? ? ? 48 FF A0 ? ? ? ?` |
| `AnimInner_RebuildTrackSlots` | `40 53 55 56 57 41 56 41 57 B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 C5 F8 29 B4 24` |
| `SeatController_StartOrUpdateTransition` | `48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC ? 48 8D 99 ? ? ? ? 48 8B F9` |
| `SeatTransition_StartHelper` | `48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B 01 48 8B F9` |
