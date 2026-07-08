# Fast Vehicle Boarding Current Boundary Analysis

## Scope

This note records the current verified boundary for fast player vehicle boarding.
It is based on targeted source inspection, current runtime log review, and targeted
IDA MCP decompilation of known functions. No global search was used.

## Confirmed Runtime Shape

The active trace plugin keeps the original player `RideOnState_OnEnter` path and
requests Drive from the `RideOnState_Update` hook after the original update returns.

The latest log shows this sequence:

```text
RideOnEnter entry
RideOnEnter exit
Attach stage 0->1
Attach stage 1->2
FastDrive requested after RideOnUpdate pose/filter pass
RideOnExit
DriveEnter
```

The fast Drive request happened at `elapsed=0.0500501`, with:

```text
cur=1 next=1 stage=2 b189=1 b18A=1 b18B=0 b191=1
```

After `DriveEnter`, the runtime had:

```text
b18B=1 b191=1 b381=0x4
```

The later dismount path reached `AnimSetState(4)`, `DismountSideClassify`, and
`RideOffPoseVariant`, which indicates the player remained on the normal player
RideOn/RideOff path rather than the passenger cargo direct-mount path.

## IDA-Confirmed Boundary

`DSPlayerVehicleRideOnState_Update` (`0x140F99C40`) only enters the Drive-completion
area while the RideOn attach stage is `2`.

Before the vanilla normal completion write at `0x140F9A2C7`, the update function
executes these two player-specific setup calls:

```text
0x140F99DF0 RideOnState_UpdateSeatPoseRequest
0x140F99DFE RideRuntime_SetRideOnActionSlotFilters
```

`RideOnState_UpdateSeatPoseRequest` resolves the active seat/action object, chooses
a RideOn pose id, writes pose request data into the owner block, marks the request
active, and sets a dirty bit.

`RideRuntime_SetRideOnActionSlotFilters` applies RideOn action slot filters for
slots `4`, `5`, `6`, `7`, `9`, `15`, and `17`, and records its applied state at
`runtime+0x24596`.

The current hook runs after the original update returns, so it preserves both calls
before writing `plugin+0x11A = 2`.

## Why Passenger Cargo Mount Is Not The Target

Passenger/human-cargo vehicle mounting uses `MountableComponent_StartMount` and then
the generic entity attach primitive. Player boarding does not call
`MountableComponent_StartMount`.

The player path has its own attach process:

```text
RideOnState_OnEnter
RideOnState_ProcessVehicleAttach
Entity_AttachToParentAndNotify
RideOnState_UpdateSeatPoseRequest
RideRuntime_SetRideOnActionSlotFilters
RideOnState_Update normal Drive transition
```

Skipping the player RideOn setup or replacing it with a direct passenger-style mount
omits player-specific pose/action setup. Previous direct-attach experiments reached
Drive quickly but corrupted later visible pose behavior, especially the first
dismount.

## Practical Conclusion

The usable intervention boundary is not `MountableComponent_StartMount`, not
`Entity_AttachToParentAndNotify` alone, and not the Drive state handler table.

The current safe boundary is:

```text
after original RideOnState_Update has run with stage == 2,
after seat pose request and action slot filters have executed,
before the vanilla timer-gated RideOn completion waits several seconds.
```

At that point, setting `plugin+0x11A = 2` mirrors the vanilla normal completion
state request while keeping the mount-side player pose/action initialization.

## 2026-07-07 Current-Version Log Review

The game updated before this review, so the older fixed RVAs are no longer valid.
Targeted IDA inspection refreshed the current addresses:

| Current address | Name | Verified role |
|---|---|---|
| `0x140F98D00` | `DSPlayerVehicleRideOnState_OnEnter` | Writes the RideOn parameter envelope and requests animation state `5`. |
| `0x140F99C60` | `DSPlayerVehicleRideOnState_Update` | Runs the stage-2 pose/filter pass and vanilla Drive completion request. |
| `0x140F9A390` | `DSPlayerVehicleRideOnState_ProcessVehicleAttach` | Player attach state machine and `Entity_AttachToParentAndNotify` caller. |
| `0x140F9B090` | `RideOnState_UpdateSeatPoseRequest` | Stage-2 RideOn seat pose request. |
| `0x1410139C0` | `RideRuntime_SetRideOnActionSlotFilters` | Stage-2 action slot filter setup. |

The trace ASI was rebuilt to resolve `DSPlayerVehicleRideOnState_Update` by the
IDA-generated signature instead of a fixed RVA. The manual run loaded that build:

```text
RideOn update fast-drive hook enabled
RideOnUpdate resolved at 0x7FF711B29C60
hook installed
```

The old experimental `CancelAnim` behavior did not appear in the log, which
confirms that the latest run was no longer using the stale cancel-animation hook.

The only fast-drive request in the manual run happened very early:

```text
FastDrive requested after RideOnUpdate pose/filter pass ... stage=2 elapsed=0.025025 b189=1 b18A=1 b18B=0 b191=1
```

This confirms the current hook writes `plugin+0x11A = 2` after stage `2` and after
the update-side pose/filter pass, but before the later `runtime+0x18B` completion
bit is set. Static inspection of the current `ProcessVehicleAttach` still shows
that `runtime+0x18B` is written in the stage-2 follow-up branch, not in `Update`
itself.

Practical implication: the next investigation should target the stage-2
`ProcessVehicleAttach` completion condition that leads to `runtime+0x18B = 1`.
Passenger cargo mount remains the wrong primary target because it bypasses the
player RideOn parameter and pose/action setup.

## 2026-07-07 Runtime Animation Dispatch Correction

The latest validated runtime trace corrected the previous assumption that the
current RideOn object uses `0x140DB9A50 -> inner+0x170 -> 0x140EF6060` for the
visible state `5` request.

Validated runtime targets from the actual RideOn object:

```text
RideOnAnimTargets ... fn20=0x...DD299A30 fn250=0x...DD29A840
innerFn168=0x...DD3A80C0 innerFn170=0x...DD3A80E0 innerFn1E8=0x...DC5810E0
```

Converted IDA addresses:

```text
0x140DBA840  component +0x250 -> write inner+0x544
0x140DB9A30  component +0x20  -> inner vtable +0x168
0x140EC80C0  inner +0x168     -> write inner+0x2E0 = state
0x1400A10E0  inner +0x1E8     -> no-op/monitor stub for this object
```

The validated boarding run logged:

```text
AnimEval entry ... state=5 ... v2E0=1 ... f544=0
AnimEval exit  ... state=5 ... v2E0=5 ... f544=0
PresentationRequest ... action=0x53758bed ... suppressed=1
ProcessAttach gate forced=1 ... b18B 0->1
FastDrive requested ... elapsed=0.0375375
```

Suppressing presentation action `0x53758BED` did not crash and did not prevent the
fast Drive state machine path from completing. It also did not remove the earlier
animation state `5` write, which occurs before the presentation request.

## 2026-07-07 Post-Drive State Reset Runtime Result

After original `DriveEnter` returned, the trace plugin called the current RideOn
anim component `+0x20` virtual function with state `1`:

```text
anim = [rideOn+0xB0]
fn20 = [anim vtable +0x20] = 0x140DB9A30
fn20(anim, 1)
```

Validated runtime effect:

```text
DriveEnter exit ... b381=0x4
AnimEval entry ... state=1 ... v2E0=5
AnimEval exit  ... state=1 ... v2E0=1
PostDriveAnimState state=1 called=1 ...
```

The run did not crash. This proves the current RideOn inner object accepts a
post-Drive transition from recorded boarding state `5` back to state `1` through
the original virtual dispatch path.
