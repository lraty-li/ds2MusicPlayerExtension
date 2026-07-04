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
