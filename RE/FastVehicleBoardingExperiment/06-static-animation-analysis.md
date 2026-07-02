# Fast Vehicle Boarding Experiment - Static Animation Analysis

## Static Animation System Analysis

Targeted IDA analysis of the animation component dispatch path:

- `0x140DB9A10` is a thin wrapper. It reads `inner = animComponent + 0x8`; if the
  pointer exists, it tail-jumps to `inner->vtable + 0x168`.
- The current vtable target is now named
  `AnimInner_SetStateAndEvaluateTracks` (`0x140EF6A20`).
- `AnimInner_SetStateAndEvaluateTracks` uses `r8b` as the visible animation state
  selector and also consumes `xmm1` as blend/phase input. This means the wrapper is
  not just setting an integer state.
- For the RideOn state value `5`, the function checks/updates inner state bytes
  around `inner + 0x54` and `inner + 0x55`, then calls
  `AnimInner_RebuildTrackSlots` (`0x140EF6040`) when the track set needs to be
  rebuilt.

`AnimInner_RebuildTrackSlots` is the first clearly identified core animation-system
piece behind mount pose setup:

- Its `rcx` argument is the animation inner object.
- It operates mainly on the object at `inner + 0x28`.
- With reset enabled, it clears clip/slot fields from roughly `+0x3B0` through
  `+0x510`.
- It then rebuilds a fixed set of 0x20-byte track slots.
- It calls `AnimTrackSlot_SelectClipByType` (`0x140EF5E90`) repeatedly with fixed
  clip type ids.

Observed static slot map:

| Clip type id | Slot output | Clip handle target |
|--------------|-------------|--------------------|
| `5` | `inner+0x28+0x458` | `inner+0x28+0x3C8` |
| `4` | `inner+0x28+0x478` | `inner+0x28+0x3C0` |
| `7` | `inner+0x28+0x498` | `inner+0x28+0x3D8` |
| `6` | `inner+0x28+0x4B8` | `inner+0x28+0x3D0` |
| `9` | `inner+0x28+0x4D8` | `inner+0x28+0x3E0` |
| `8` | `inner+0x28+0x4F8` | `inner+0x28+0x3E8` |

`AnimTrackSlot_SelectClipByType` reads the clip collection at `rcx + 0x50`, selects
the first clip matching the requested type id, and writes the selected slot:

- `slot + 0x00`: clip count/index field
- `slot + 0x04`: current time/value reset to `0.0`
- `slot + 0x0C`: default rate/weight selected from clip subtype
- `slot + 0x18`: active flag set to `1`
- target pointer receives the selected clip handle

Static conclusion: RideOn animation state `5` is a high-level request that can cause
the animation inner object to rebuild a multi-track clip set. The skip-OnEnter path
bypassed both the RideOn parameter setup and this track-set context, so the
standing-on-seat failure is consistent with missing multi-track pose/clip
initialization rather than a missing single state enum write.

## RideOn Action Parameter Block Layout

Static reading of `DSPlayerVehicleRideOnState_OnEnter` shows that `rideOn + 0x98`
points to a large action/animation parameter block. The common pattern is:

```text
if (current_value != desired_value) {
    *(byte *)(params + value_offset - 0x10) |= 0x04;
    *(float/int *)(params + value_offset) = desired_value;
}
```

Examples from the RideOn OnEnter middle section:

| Flag offset | Value offset | OnEnter value |
|-------------|--------------|---------------|
| `+0x3940` | `+0x3950` | computed pose scalar (`xmm10`) |
| `+0x34C0` | `+0x34D0` | `0` |
| `+0x3970` | `+0x3980` | `0` |
| `+0x3700` | `+0x3710` | `0` |
| `+0x3730` | `+0x3740` | `-1.0` |
| `+0x3760` | `+0x3770` | runtime value from `runtime + 0x240` |
| `+0x3790` | `+0x37A0` | `0` |
| `+0x3820` | `+0x3830` | `0` |
| `+0x3880` | `+0x3890` | `0` |
| `+0x3DC0` | `+0x3DD0` | `1.0` |
| `+0x53B0` | `+0x53C0` | `0` |

The later flag-only block sets a sequence of action parameter enable bytes:

```text
params+0x1030, +0x1060, +0x1090, +0x10C0, +0x10F0,
+0x1120, +0x1150, +0x1180, +0x11B0, +0x11E0,
+0x1210, +0x1510
```

Most use `flag = (flag & 0xED) | 0x04`; some conditional paths use `| 0x14`,
which preserves the normal valid bit and adds an extra mode bit. The dynamic
`RideOnPoseParams` trace saw this as `fl1030 1->5`, `fl1090 33->37`,
`fl1120 225->229`, `fl1510 1->5`, and `fl3910 1->5`.

Static conclusion: the original OnEnter sets a broad action-parameter envelope
before calling animation state `5`. A valid fast-boarding intervention has to keep
this parameter envelope or reproduce it; otherwise the inner animation tracks may
exist without the correct pose parameters.

## Presentation Helpers Are Separate

Static review of the global presentation/action helpers distinguishes them from the
pose setup above:

- `PresentationGlobal_RequestAction` (`0x140E21860`) writes a request hash/id,
  mode, and target pointer into the global object at `qword_14623E908`.
- `PresentationGlobal_WriteActionParam` (`0x140E21970`) writes one integer/float
  parameter pair into the same global object.
- These helpers do not write the `rideOn + 0x98` action-parameter envelope and do
  not rebuild the animation inner track slots.

This explains the earlier negative result: suppressing presentation/global action
requests can remove or change presentation-layer behavior, but it cannot by itself
restore the seated pose after the skip-OnEnter path bypassed the per-state
parameter envelope and multi-track animation setup.

## RideOn Update Completion And Action Slots

Targeted static analysis of `DSPlayerVehicleRideOnState_Update` (`0x140F99C40`)
identifies two different completion exits:

- `0x140F99FCC`: `ActionParams_QueryBoolByParamId(params, 0xED)` queries a
  parameter/event exposed by the action parameter object. If this returns true,
  control enters the normal completion path.
- `0x140F99FFC`: elapsed time at `state+0x180` is compared with
  `dword_143461CCC`. If the timer threshold is met, control also enters the normal
  completion path.
- `0x140F9A0A2`: the pre-threshold early-finish branch writes
  `plugin+0x11A = 3`. This is the path hit by the failed `runtime+0x18B` preseed
  experiment and is not a valid Drive target.
- `0x140F9A2C7`: the normal completion path emits event data and writes
  `plugin+0x11A = 2`, which is the Drive transition target.

`ActionParams_QueryBoolByParamId` (`0x140DBEA00`) is now named in IDA. It calls the
parameter object vtable `+0xA0` to map the external id and then calls the object at
`params+0x8A8` vtable `+0xE0` as a boolean query. This confirms `0xED` is an
action/animation graph signal, not a simple RideOn struct byte.

`RideOnState_UpdateSeatPoseRequest` (`0x140F9B070`) is called while
`state+0x198 == 2`. It resolves the current seat/vehicle action object, chooses a
seat pose id based on runtime mount stage and seat clip counts, and writes the
request into the pose/action owner block:

- `+0x788`: selected RideOn seat pose id.
- `+0x7BC`: companion value `15`.
- `+0x2104`: pose request active byte set to `1`.
- `+0x2154`: dirty bit `0x1000` set.

Observed static pose ids include `24`, `25`, `26`, `27`, `28`, `30`, `31`, `33`,
`34`, `36`, and `37`. These values vary by stage and seat layout. This function is
therefore the first confirmed RideOn update-side seat-pose builder, separate
from the earlier OnEnter parameter envelope.

`RideRuntime_SetRideOnActionSlotFilters` (`0x1410139A0`) is also called from the
same update block. It toggles action slots `4`, `5`, `6`, `7`, `9`, `15`, and `17`
and stores its applied state at `runtime+0x24596`. It uses:

- `ActionTree_FindSlotByByteId` (`0x141198FB0`) to locate action-tree slots by a
  one-byte id.
- `ActionSlot_SetTagFiltered` (`0x141194500`) to add or remove tag `0x12C73EB0`
  from a slot filter list.
- `PlayerActionFilters_SetSlotTag` (`0x140F779C0`) to add or remove tag
  `0x1221D954` for player action filter slots `9` and `15`.

Conclusion: the reasonable fast-boarding intervention boundary is after original
`RideOnState_OnEnter` has established the parameter envelope, after normal
`ProcessVehicleAttach` has reached `stage == 2`, and after the RideOn update-side
pose/filter setup has been allowed to execute at least once. The target transition
is the normal `plugin+0x11A = 2` completion semantics, not the pre-threshold
`plugin+0x11A = 3` early-finish branch and not any presentation/entity suppressor.

Follow-up static reading of the pose selector split the seat action helpers:

- `SeatAction_CountClipType` (`0x141F6B060`) counts entries in a 16-bit seat action
  type table. RideOn pose selection counts type ids `93`, `92`, `97`, and `98`.
- `SeatAction_HasProgressGate` (`0x141F6C0B0`) returns true only when the seat
  action object has `+0x4FC` nonzero, `+0x5C0` present, `+0x4E4 == 2`, and the
  float at `+0x5C0+0x14` is greater than `dword_14346137C`.

This explains why the Update-after-pose candidate preserves the pose/action setup but
does not remove the visible long boarding animation: the pose request builder reads
the current seat action progress and writes a pose request, but it does not advance
the action graph completion signal `0xED`. A more native fast path should enter the
normal `RideOnState_Update` completion block, which emits its event data before
writing `plugin+0x11A = 2`, rather than writing `next=2` externally after Update.
