# Fast Vehicle Boarding Experiment

## Scope

This document records the currently verified fast vehicle boarding experiment for the
player RideVehicle path. It only includes observed behavior from the current trace
plugin and IDA-confirmed state fields.

## Current Source State 2026-07-02 23:40

The current `ds2_vehicle_boarding_trace` build is the safe RideOn-preserving trace
baseline. It installs:

- `DSPlayerRideVehicleActionPlugin_Init` (`0x1410047B0`)
- `DSPlayerVehicleRideOnState_ProcessVehicleAttach` (`0x140F9A370`)
- `DSPlayerVehicleRideOnState_OnExit` (`0x140F99990`)
- `DSPlayerVehicleDriveState_OnEnter` (`0x140F8EB40`)
- `DSPlayerVehicleRideOnState_ClassifyBoardingApproach` (`0x140F9B4A0`)
- `DSPlayerVehicleRideOnState_OnEnter` (`0x140F98CE0`)
- RideOn animation-component state wrapper (`0x140DB9A10`)
- seat/controller transition helper (`0x141F6BDC0`)
- RideOff side/pose trace hooks
- RideOn pose/action parameter trace
- RideOn update hook (`0x140F99C40`)
- Drive action push trace as log-only

It no longer installs the earlier exploratory suppressors for:

- `PresentationGlobal_RequestAction` (`0x140E21860`)
- `PresentationGlobal_WriteActionParam` (`0x140E21970`)
- `sub_140B198E0` Drive-entry action-list push
- `sub_1401618C0` RideOn entity message send
- `sub_140F9B670` approach-derived seat state write

The current behavior change is:

1. `RideOnState_OnEnter` calls the original OnEnter body.
2. `ProcessVehicleAttach` is allowed to run normally.
3. `RideOnState_Update` is allowed to run normally after attach stage `2`.
4. After original `RideOnState_Update` returns, if the snapshot is still
   `current == 1`, `next == 1`, `stage == 2`, `b18A == 1`, and `b191 == 1`, the
   hook requests the normal Drive transition by writing
   `*(uint16_t *)(plugin + 0x11A) = 2`.

All animation-component, RideOff, pose-parameter, action-slot, and seat/controller
hooks are read-only in this source state. The RideOn update hook changes only the
post-update transition request. Earlier skip-OnEnter direct attach testing showed
that missing mount-side pose/action parameter setup can cause the first dismount to
inherit an invalid pose state.

