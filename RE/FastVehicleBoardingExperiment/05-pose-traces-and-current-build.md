# Fast Vehicle Boarding Experiment - Pose Traces and Current Build

## Skip-OnEnter Direct Attach Dismount Pose Observation

2026-07-02 manual validation of the skip-OnEnter direct attach path observed that
after the first dismount the player character played an incorrect dismount pose,
fell forward, and ended up away from the vehicle. Later boarding attempts then
looked like they were continuing or reusing a dismount-side animation state.

The runtime log explains the failure mode:

```text
AnimSetState call state=4 caller=0x140F97761
AnimSetState call state=1 caller=0x140F97B36
```

`state=4` is emitted by `DSPlayerVehicleRideOffState_OnEnter`, not by a later
boarding attempt. Therefore the visible "wrong boarding animation" after the first
cycle is a downstream effect of corrupt or missing mount-side pose setup, not proof
that RideOn is directly requesting the RideOff animation.

The skip-OnEnter path bypassed the original `DSPlayerVehicleRideOnState_OnEnter`,
including:

- the pose/action parameter writes into the object at `rideOn + 0x98`
- `AnimSetState state=5` for mount-side animation setup
- action/global parameter writes used by the presentation/action layer

The current build keeps the original RideOn OnEnter and requests Drive only after
normal player `ProcessVehicleAttach` reaches stage `2` and one original
`RideOnState_Update` pass has returned.

## RideOff Pose Trace

Two new read-only trace hooks were added after the manual regression:

- `DSRideRuntime_ClassifyDismountSide` (`0x14100FF60`)
- `DSPlayerVehicleRideOffState_SelectDismountPoseVariant` (`0x140F98A40`)

2026-07-02 automated `test_boarding.ps1` run with the safe baseline:

```text
RideOnEnter entry ...
AnimSetState call state=5 caller=0x140F99892
Attach stage 0->1
Attach stage 1->2
FastDrive requested after ProcessVehicleAttach stage 2
RideOnExit entry/exit
DriveEnter entry/exit ... b18B=1 b191=1 b381=0x4
AnimSetState call state=4 caller=0x140F97761
DismountSideClassify result=2 ... kind=1 ... sideX=0 sideZ=0 flags7358=0
RideOffPoseVariant side=2 result=0 ... kind=1 runtime+2A4=4
AnimSetState call state=1 caller=0x140F97B36
```

This confirms the safe baseline still completes the automated board, dismount, and
quit flow, and that the first dismount uses a side/pose selection chain after
`state=4`.

Current conclusion: any fast-boarding candidate must preserve the RideOn pose/action
initialization that feeds the later RideOff side and pose variant chain. Skipping the
entire RideOn OnEnter causes the observed dismount pose failure unless that setup is
preserved or reproduced elsewhere.

## Animation Wrapper Trace

2026-07-02 targeted IDA analysis and read-only trace focused on the animation
component wrappers referenced from `RideOnState_OnEnter`:

- `0x140DB9A10`: thin `AnimSetState` wrapper. It reads
  `inner = animComponent + 0x8`; if non-null, it tail-jumps through
  `inner->vtable + 0x168`.
- `0x140DBA820`: vtable `+0x250` wrapper. It reads the same inner pointer and writes
  the float parameter from `xmm1` to `inner + 0x544`.

Safe-baseline automated run:

```text
RideOnEnter entry ...
AnimFloat544 caller=0x140F99880 ... value=0 f544 4->0
AnimSetState call state=5 caller=0x140F99892
AnimInner before state=5 ... v3A0=1065353216 f544=0 flags760=0x0
AnimInner after  state=5 ... v3A0=1065353216 f544=0 flags760=0x0
...
AnimSetState call state=4 caller=0x140F97761
AnimInner before/after state=4 ... f544=0
...
AnimSetState call state=1 caller=0x140F97B36
AnimInner before/after state=1 ... f544=0
AnimSetState call state=1 caller=0x140FB4096
AnimInner before/after state=1 ... f544=4
```

Observed result: the sampled inner fields did not change across the `state=5`,
`state=4`, or `state=1` wrapper calls themselves. The clearly observed mount-side
change was the preceding `inner + 0x544` reset from `4` to `0` before `state=5`.

This supports the current model: correct RideOn pose setup is not encoded by a
single `AnimSetState(5)` call. It includes surrounding animation parameters and
phase/timer state written before the state request, plus the large RideOn action
parameter block. A reasonable intervention must preserve these setup writes while
targeting only the long presentation/movement portion.

## RideOn Pose Parameter Trace

The current trace also records selected fields from the object at `rideOn + 0x98`
before and after original `DSPlayerVehicleRideOnState_OnEnter`.

2026-07-02 safe-baseline automated run:

```text
RideOnPoseParams entry ...
  kind=0 variant=1 b18A=0 b18B=0 b191=0
  f3830=0 f3890=0 f3920=0 f3950=0 f3980=0 f3A70=0 f3DD0=0 f53C0=0
  fl1030=1 fl1090=33 fl1120=225 fl1510=1 fl3910=1 fl3970=1

RideOnPoseParams exit ...
  kind=1 variant=1 b18A=0 b18B=0 b191=1
  f3830=0 f3890=0 f3920=1 f3950=0 f3980=0 f3A70=0 f3DD0=1 f53C0=0
  fl1030=5 fl1090=37 fl1120=229 fl1510=5 fl3910=5 fl3970=1
```

Confirmed OnEnter effects in this run:

- `runtime + 0x2A0` (`kind`) changed `0 -> 1`.
- `runtime + 0x191` changed `0 -> 1`.
- pose/action parameter `params + 0x3920` changed `0 -> 1`.
- pose/action parameter `params + 0x3DD0` changed `0 -> 1`.
- flags at `params + 0x1030`, `+0x1090`, `+0x1120`, `+0x1510`, and `+0x3910`
  changed from the pre-OnEnter values to their RideOn-active values.
- `runtime + 0x2A4` was still `1` immediately after OnEnter, but the later
  RideOff pose trace observed `runtime + 0x2A4 = 4`, so that field is changed
  later in the board/drive/dismount chain.

Interpretation: the original OnEnter body establishes a pose/action parameter set
that the skip-OnEnter path did not reproduce. The next useful intervention point is
not the state wrapper itself; it is the boundary between this required setup and the
longer presentation/movement that follows.

Follow-up with `runtime + 0x2A4` added to the normal state snapshot:

```text
ClassifyApproach ... kind=1 variant=1 stage=0
RideOnExit entry ... kind=1 variant=1 stage=2
DriveEnter entry ... kind=1 variant=1 stage=2
DriveEnter exit ... kind=1 variant=1 stage=2
RideOffPoseVariant side=2 ... kind=1 runtime+2A4=4
```

This narrows `runtime + 0x2A4`: it remains `1` through the fast Drive entry and only
appears as `4` when the RideOff pose-variant selector runs. It is not the missing
mount-side seated-pose field for the skip-OnEnter path; it belongs later in the
dismount-side pose chain or is changed by the state transition into RideOff.

## Current Trace Build State

After the animation wrapper, pose-parameter, and RideOn update findings, the ASI is
a RideOn-preserving fast-drive candidate:

- original `RideOnState_OnEnter` is preserved
- normal `ProcessVehicleAttach` is allowed to reach `stage == 2`
- original `RideOnState_Update` is allowed to run once after stage `2`
- fast Drive request happens after that original update pass returns, with
  `current == 1`, `next == 1`, `stage == 2`, `b18A == 1`, and `b191 == 1`
- `AnimSetState` wrapper logs state calls and sampled inner fields
- animation vtable `+0x250` logs `inner + 0x544` writes
- `RideOnPoseParams` logs selected `rideOn + 0x98` parameter fields
- RideOff side/pose trace logs `DismountSideClassify` and
  `RideOffPoseVariant`
- Drive action push trace is installed as log-only; it no longer suppresses action
  `0x49`

2026-07-02 automated `test_boarding.ps1` validation of the update-after-pose
candidate completed board, dismount, and quit without crash:

```text
Attach stage 0->1
Attach stage 1->2
FastDrive requested after RideOnUpdate pose/filter pass ... stage=2 elapsed=0.0500501 b18A=1 b191=1
RideOnExit entry ... next=2 elapsed=0.0500501
DriveEnter exit ... b18B=1 b191=1 b381=0x4
RideOffPoseVariant side=2 result=0 ... runtime+2A4=4
```

`capture_boarding_visual.ps1` also completed with this candidate. The first-board
screenshots show the original mount animation still visible at 150 ms, 500 ms,
and 1200 ms. This confirms the new boundary preserves the required pose setup but
does not by itself remove the long visible boarding animation.

