# Project Goal — Player Instant-Board Mod

## Real goal

Make a mod so that when the player boards a vehicle, the player is teleported directly onto the
vehicle (direct attach), skipping the long RideOn animation.

The user's hypothesis: the game already does this for human-cargo transport (the human is
teleported directly onto the vehicle), and that cargo mount path is reusable for the player.

## What the existing analysis already established

From `RE/PassengerCargoVehicleMountAnalysis.md`:

- Cargo / human-cargo mount chain (no ride-on animation):
  `PassengerCargo_UpdateMaybeVehicleMount` (`0x1408E2CD0`)
  -> `PassengerCargo_CanStartVehicleMount` (`0x1408E6A90`)
  -> `PassengerCargo_SelectSlotAndStartMount` (`0x1408E6F50`)
  -> `MountableComponent_StartMount` (`0x1402F1EF0`)
  -> `Entity_AttachToParentAndNotify` (`0x140130900`) at `0x1402F1F43` (immediate attach).

- Player boarding chain (has RideOn animation):
  `DSPlayerRideVehicleActionPlugin` -> state 1 RideOn (animation) -> state 2 Drive.
  Player RideOn does NOT call `MountableComponent_StartMount`.
  Player attach happens inside `DSPlayerVehicleRideOnState_ProcessVehicleAttach` stage 0 via
  `Entity_AttachToParentAndNotify` at `0x140F9AD80`, but only after the RideOn state machine
  runs the boarding animation (~3.6s in the observed run) before transitioning to Drive.

- Shared primitive: `Entity_AttachToParentAndNotify` is used by BOTH paths. The difference is
  that the cargo path attaches immediately with no RideOn state machine; the player path runs
  the RideOn animation state machine first.

## Key question for this mod

Is there a data/state-driven way (no register-context hook, no callsite patch as final
solution) to make the player board via the direct-attach path (`MountableComponent_StartMount`
/ `Entity_AttachToParentAndNotify`) instead of the RideOn animation state machine?

Sub-questions:
1. Who calls `MountableComponent_StartMount` (`0x1402F1EF0`)? Is there any player-side caller,
   or only `PassengerCargo_SelectSlotAndStartMount` (`0x1408E7C89`) and
   `MountableComponent_ExportedMount_SelectSlot` (`0x1402F3B9C`)?
2. Can the `MountableComponent_StartMount` mounter argument be the player entity, or is it
   restricted to cargo/passenger entities by the caller?
3. Is there a data flag/condition that routes the player's board action (F key) to
   `MountableComponent_StartMount` instead of `DSPlayerRideVehicleActionPlugin`?
4. Failing that, is there a data path to make the RideOn state complete instantly (skip
   animation) — e.g. gate 2 (`rideRuntime+0x190` b190 && `+0x192` b192), armed when
   `state+0xA8->0x3970 == 9`?

## Constraint reminder

Final solution must be data/state-driven. No entry detour / callsite patch / gateway / stub /
naked asm / register save-restore as the final mechanism. Read-only observation hooks for
verification are allowed. No runtime register forging.
