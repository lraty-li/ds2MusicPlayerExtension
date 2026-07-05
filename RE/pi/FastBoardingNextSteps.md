# Fast Vehicle Boarding — Exploration Strategy & Next Steps

## Date: 2026-07-05

## Current State

After fixing the VEX trampoline bug (see `TrampolineVexBug.md`):
- `ds2_vehicle_boarding_trace` hooks `RideOnState_Update` (at `0x140F99C60`)
- After original Update runs, the hook checks conditions:
  - `current == 1` (RideOn state)
  - `next == 1` (no pending transition)
  - `stage == 2` (ProcessVehicleAttach completed)
  - `b18A == 1` (stage 1 attach confirmed)
  - `b191 == 1` (OnEnter completed)
- If all conditions met: writes `plugin+0x11A = 2` (Drive)
- Expected latency: ~0.05s (one Update frame after stage 2)

## Remaining Issue: Pose Settling Artifact

Prior experiments (documented in `PassengerCargoVehicleMountAnalysis.md`) showed
that the 0.05s fast-Drive approach works but leaves a short pose/position
settling artifact. The player appears at the seat but a brief adjustment
animation plays before the seated idle pose.

### Why the artifact exists

Per `RideOnRealtimeAnimationArchitecture.md`, the vanilla 3.55s RideOn animation
accumulates per-frame state:

1. **`seat+0x1268` blend value** — written every frame by
   `RideRuntime_UpdateSeatAnimationData` (`0x1410122A0`). Starts at 0,
   accumulates over ~3.6s of per-frame updates. Controls the pose blend between
   the mount-climb animation and the seated idle.

2. **Inner animation track slots** — rebuilt during OnEnter's
   `AnimSetState(5)` call. During the 3.6s, transitional clips play
   (climb, enter), blending into the seated pose. Skipping the time means
   these clips haven't played through their blend-in period.

3. **Animation blend weights** — per-track weight values that transition from
   0→1 or 1→0 during the animation. Without the time, these are at their
   initial values.

## Exploration Directions

### Direction A: Pre-set seat blend value

Before triggering Drive transition, write the correct `seat+0x1268` value
that would normally be reached after ~3.6s of animation updates.

Questions to answer:
- What is the Drive-state value of `seat+0x1268`?
- Does the value depend on vehicle type / boarding context?
- Can we read it from a successful normal boarding and replay it?

Investigation path:
1. Hook `RideRuntime_UpdateSeatAnimationData` and log `seat+0x1268` values
   during both a fast-Drive and a vanilla boarding
2. Compare values to identify the target
3. Write the target value before triggering Drive

### Direction B: Skip animation processing in command dispatcher

The per-frame animation in Layer 1 (command dispatcher at `0x14100AF30`) runs
cmd 6 (main animation update) every frame. If we could suppress cmd 6
processing after the pose/filter setup is done, the animation state wouldn't
need to accumulate over time.

Questions:
- Can we make cmd 6 return immediately without doing its animation work?
- What state does cmd 6 leave if it doesn't run?

### Direction C: Let animation "complete" in one frame

Instead of suppressing animation, run all the accumulated animation updates
in a single frame. This would require understanding the timer/threshold
system that gates animation completion.

### Direction D: Investigate human cargo boarding for pose reference

Human cargo is directly attached to the vehicle without animation. Studying
what state the cargo entity has after direct mount (specifically its
animation/pose state) could reveal what the "correct" seated state should be.

Questions:
- What animation state does a cargo entity have after `MountableComponent_StartMount`?
- Can we copy that state to the player entity?

## Immediate Verification Needed

Once the fixed hook is tested in-game:
1. Verify the hook fires (UpdateTick logs appear)
2. Verify the conditions are met and FastDrive is requested
3. Screenshot the first few frames after Drive transition
4. Observe the dismount pose correctness

## Known Risks

- The conditions (stage==2, b18A==1, b191==1) may not all be satisfied in the
  first Update frame. If ProcessVehicleAttach hasn't been called yet, the hook
  won't trigger and normal boarding will proceed.
- The Drive OnEnter at `0x140F8EB60` may need additional setup that vanilla
  Drive has but fast-Drive doesn't (e.g., the event broadcast at the end of
  RideOn Update).
