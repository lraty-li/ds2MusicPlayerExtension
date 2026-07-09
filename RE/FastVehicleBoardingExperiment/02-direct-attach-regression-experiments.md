# Fast Vehicle Boarding Experiment - Direct Attach Observations

## 2026-07-02 Manual Skip-OnEnter Direct Attach Observation

User visual testing of the skip-OnEnter direct attach build showed that after the
first cycle, later boarding attempts visually reused a dismount-side animation
family.

Runtime log from `log.txt` confirms the state-machine part of that build still
worked for repeated attempts:

```text
21:19:38 RideOnEnter skipped original, direct attach stage=2, next=2
21:19:38 RideOnExit -> DriveEnter in elapsed=0.0166834
21:19:46 RideOnEnter skipped original, direct attach stage=2, next=2
21:19:46 RideOnExit -> DriveEnter in elapsed=0.0166834
21:20:32 RideOnEnter skipped original, direct attach stage=2, next=2
21:20:32 RideOnExit -> DriveEnter in elapsed=0.0208542
21:21:15 RideOnEnter skipped original, direct attach stage=2, next=2
21:21:15 RideOnExit -> DriveEnter in elapsed=0.0166834
21:21:34 RideOnEnter skipped original, direct attach stage=2, next=2
21:21:34 RideOnExit -> DriveEnter in elapsed=0.0166834
```

The same log shows the suspicious animation-component calls are tied to RideOff and
cleanup, not to the next RideOn enter:

```text
21:19:42 AnimSetState state=4 caller=0x140F97761
21:19:43 AnimSetState state=1 caller=0x140F97B36
21:19:43 SeatTransition start=0 caller=0x1410050E4
21:19:43 AnimSetState state=1 caller=0x140FB4096
```

Targeted IDA analysis of those callers:

- `0x140F97680` renamed to `DSPlayerVehicleRideOffState_OnEnter`.
- `0x140F9775E` is the RideOff OnEnter call to animation component vtable `+0x20`
  with `state=4`. Runtime caller appears as `0x140F97761`.
- `0x140F97AE0` renamed to `DSPlayerVehicleRideOffState_OnExit`.
- `0x140F97B33` is the RideOff OnExit call to the same animation state wrapper with
  `state=1`. Runtime caller appears as `0x140F97B36`.
- `0x141004F65` is a broader RideVehicle cleanup/reset path that also requests
  animation state `1`; runtime caller appears as `0x141004F68`.
- `0x140FB4093` is another state/reset path that requests animation state `1`;
  runtime caller appears as `0x140FB4096`.

Interpretation: the later bad boarding visuals are not caused by a fresh
`state=4` call during `RideOnState_OnEnter`; the skip-OnEnter path bypasses the
original RideOn OnEnter entirely, so the boarding attempt does not submit a replacement
boarding/seat animation-component state. After a RideOff cycle, the visual system
can therefore continue from the recently used RideOff/cleanup action chain while
the RideVehicle state machine already moves to Drive.

Conclusion: same-frame attach and Drive entry are possible, and repeated
board/dismount cycles still depend on preserving or reproducing the skipped
mount-side pose/action setup. The key boundary is the RideOn pose state selection
path rather than additional presentation/entity suppressors.

## 2026-07-02 Direct Pre-Animation State Experiments

Two narrow experiments were tested after the manual observation. Both kept the
skip-OnEnter structure: skip original `RideOnState_OnEnter`, call original
`ProcessVehicleAttach` twice, then request Drive when `stage == 2`.

Experiment A: call the original animation-component wrapper with `state=1` before
the two direct attach calls.

Observed log:

```text
RideOnEnter direct pre-reset AnimSetState state=1
RideOnEnter original skipped; direct attach attempted=1 ... stage=2
RideOnExit -> DriveEnter in elapsed=0.0166834
```

Screenshot result from `capture_boarding_visual.ps1`:

- `01_board_150ms.png`: the player still appears offset above/near the vehicle.
- `02_board_500ms.png`: the player is still in the same broad transient pose family.

Experiment B: call the original animation-component wrapper with `state=5` before
the two direct attach calls. This matches the state value normally requested by
original `RideOnState_OnEnter`, but without running the rest of original OnEnter.

Observed log:

```text
RideOnEnter direct pre AnimSetState state=5
RideOnEnter original skipped; direct attach attempted=1 ... stage=2
RideOnExit -> DriveEnter in elapsed=0.0166834
```

Screenshot result:

- `01_board_150ms.png`: the player still appears in an incorrect offset/airborne
  pose near the vehicle.

Conclusion: a single pre-attach animation-component state request does not recreate
the full mount-side pose setup used by the normal RideOn path. These experiments
were removed from the current ASI.

`capture_boarding_visual.ps1` now captures only the first board and the
post-dismount state:

```text
00_before_board.png
01_board_150ms.png
02_board_500ms.png
03_board_1200ms.png
04_board_2200ms.png
05_board_3700ms.png
06_after_dismount.png
```

The earlier scripted second board attempt was removed. In the tested garage
position, the reposition step often selected a private-room or unrelated prompt
instead of the vehicle prompt, and the log contained no second `RideOnEnter`. Those
stale reboard screenshots are not valid evidence for repeated boarding behavior.

## 2026-07-02 Original OnEnter Plus Immediate Attach Experiment

Experiment: allow the original `RideOnState_OnEnter` body to run, then immediately
call the original `ProcessVehicleAttach` trampoline twice and request Drive when
`stage == 2`. This differs from the historical fast-path hook because the attach
and Drive request happen inside the OnEnter hook after original OnEnter returns,
not later from naturally scheduled attach calls.

Runtime result:

```text
RideOnEnter entry ...
AnimSetState call state=5 caller=0x140F99892
ClassifyApproach result=0 ... stage=0 ... b18A=1
RideOnEnter original ran; post attach attempted=1 ... next=2 ... stage=2
RideOnExit -> DriveEnter in elapsed=0.0166834
```

Screenshot result:

- `01_board_150ms.png`: player is visibly in the original climb/boarding pose on
  the vehicle side.
- `02_board_500ms.png`: player is still visibly climbing over the vehicle body.

Conclusion: running original `RideOnState_OnEnter`, even followed by same-frame
direct attach and Drive request, preserves the long climb animation. The useful
state for pose convergence is therefore not obtained by simply running original
OnEnter first.

## 2026-07-02 Clear `b18A` Before Drive Experiment

Targeted IDA reading of `DriveState_OnEnter` shows that if `runtime + 0x18A` is
zero, Drive entry executes its own seat transition start path:

```text
0x140F8ED9C: sub_141F6BDC0(..., start=1, callback=runtime+0x2A8, finishFlag=0)
0x140F8EDC0: runtime+0x18A = 1
```

The skip-OnEnter direct attach path reaches Drive with `b18A=1` because the direct `ProcessVehicleAttach`
calls have already set it. Experiment: after the two direct attach calls and before
requesting Drive, write `runtime + 0x18A = 0` so that Drive entry runs this start
path.

Runtime result:

```text
RideOnEnter direct cleared b18A before Drive request
RideOnEnter original skipped; direct attach attempted=1 ... stage=2 b18A=0
DriveEnter entry ... b18A=0 b18B=0 b381=0x0
SeatTransition call caller=0x140F8ED97 ... start=1 callback=runtime+0x2A8
DriveEnter exit ... b18A=0 b18B=0 b381=0x0
```

After Drive entry, the normal Drive flags were not established. The log then showed
repeated `AnimSetState state=3` / `state=1` calls, and screenshots showed the player
standing near/on the vehicle with cargo prompts rather than entering a stable seated
Drive state.

Conclusion: clearing `b18A` before Drive routes Drive entry through its own seat
transition start path and changes the downstream state outcome. `runtime + 0x18A`
must remain set after the direct `ProcessVehicleAttach` sequence. This was a
temporary skip-OnEnter direct attach experiment and is not present in the current
ASI.

