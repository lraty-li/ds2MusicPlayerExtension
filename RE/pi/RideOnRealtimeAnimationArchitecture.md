# RideOn Realtime Animation Architecture

## Scope

This note records the two-layer per-frame animation processing that consumes the
3.6 s vanilla RideOn time. It extends `re/FastVehicleBoardingAnimationSystem.md`
with the realtime architecture discovered via targeted IDA MCP analysis
(2026-07-05). All facts are verified from decompilation; no global search used.

## Two-Layer Architecture

During RideOn, per-frame animation work is split across two independent layers,
both firing every frame:

| Layer | Entry | Owner | Role |
|---|---|---|---|
| Layer 1 | `0x14100AF30` | RideOn plugin state machine | High-level command dispatch |
| Layer 2 | `0x140F99C40` | RideOn state object | Pose/filter/completion logic |

Both run per-frame. The 3.55 s gap between stage-2 pose setup and the Drive
transition is consumed by both layers running every frame until the timer gate
is satisfied.

## Layer 1: State Command Dispatcher (0x14100AF30)

### Dispatch mechanism

The dispatcher is loaded as the RideOn plugin's per-frame handler via the function
table at `funcs_140FE45AA[1]` (`0x142E0C9D0+8`). It is called by
`DSPlayerRideVehicleActionPlugin_ApplyStateTransition` at `0x140FE4560`.

Prototype: `void(plugin, delta, cmdId, extra)`

The `cmdId` switch dispatches to:

| Cmd | Role | Details |
|---:|---|---|
| `0` | ON_ENTER | RideOn init: `sub_141010A10`, `sub_140130C60(MsgDsBaggageEvent)`, `animComp->vtable+0x20(4)`, `sub_14100EF50`, `sub_141004020`, `sub_140DBF820(params,14,&buf)` |
| `1` | ON_EXIT | Cleanup: `sub_14100F8D0`, `sub_14100EF10`, clears flags on `plugin+0x11C` |
| `2` | Timing/path | `sub_141010580`: positional math + `sub_140FC45C0(slot=21)` path query; may trigger `sub_140F4D270` (flag setter) |
| `3` | Completion transition | Elapsed vs `dword_143460F8C` check, animation blend via `vtable+0x968/+0x976/+0x600/+0x32` |
| `5` | Camera/transform copy | Copies `plugin+0x30` YMM data to `plugin+0xE8` (player pos → rideRuntime transform) |
| `6` | **Main animation update** | See below |
| `7` | Param-gated update | `ActionParams_QueryBoolByParamId(0x94)`, calls `vtable+0x2A8(1,0)` + `vtable+0x3D8(0)` if true |
| `10` | Extra | `sub_14100F2E0` |

Cmd `4`, `8`, `9` fall through to default (no-op / vzeroupper return).

### Command 6 — main animation update

Cmd 6 is the heaviest command. It runs unconditionally and calls these functions
in order:

1. **Flag-185 handler** (`0x14100B66F-0x14100B6F0`):
   If `plugin+0xE8+0xB8 (0x740)[185] & 1`: clears the bit, writes `[154]=2`,
   `[184]=0`, `[186]=2`. Then updates `plugin+0x30+0x1030 (4144) &= 0xE9 | 0x14`,
   `plugin+0x30+0x3820 (14368) |= 4`, `plugin+0x30+0x3830 (14384) = 1.0f`.
   Calls `sub_141011700(plugin)`.

2. **`sub_141011700`** (0x141011700) — seat pose offset write:
   - Resolves seat via `rideRuntime+0x38+0xC68` → `sub_1401783C0` → `word_1444BDB10`
   - If seat match and `seat_obj[607] != 0 && seat_obj[1220] != 12`: writes `seat_obj[1221] = 12`
   - Second seat family (`word_1444BAD20`): if match, writes `seat_obj[4700] = 16`

3. **`sub_14100FAD0`** (0x14100FAD0) — seat baggage update:
   - Resolves seat via `rideRuntime+0x38+0xC68` → `sub_1401783C0` → `word_1444BCA50`
   - If seat match: calls `sub_141007450(plugin)` then `DSPlayerVehicleRideOnState_UpdateSeatBaggageFlags(plugin, seat)`
   - If no seat match: calls only `DSPlayerVehicleRideOnState_UpdateSeatBaggageFlags(plugin, 0)`

4. **`sub_14100FBE0`** (0x14100FBE0) — seat entity signal:
   - Resolves seat via `sub_1401783C0` → `word_1444BCA50`
   - If seat match and `seat+0x557 != 1`: calls `sub_140138810(seat, 1)`
     - `sub_140138810` recursively sets `seat+0x557 = value` then broadcasts
       to children under SRW lock via `AcquireSRWLockExclusive`

5. **Flag 0x1000 check** (`0x14100B730-0x14100B744`):
   If `plugin+0x188` (b392) is set, `plugin+0x1A4` (b420) is 0, and
   `plugin+0x28+0x7360 (29536) & 0x1000`: sets `plugin+0x1A4 = 1` and calls
   `sub_140FC4FB0(plugin+0xB8)`.

### Helper: `sub_140FC4FB0` (0x140FC4FB0)

A large spatial/animation function called from cmd 6 when flag 0x1000 is set:
- Reads player position/orientation from `plugin+0x30`
- Computes 3D spatial queries via `sub_140E4A840`, `sub_1400D11C0`, `sub_1400D1D30`, `atan2f`
- Calls `sub_1424510E0` (collision/spatial query)
- Broadcasts `MsgDsNotifyFromPlayer` via `sub_140130C60` to action-slotted entities
- Calls `sub_1413A90F0` with event `0x63C9EC60` at the end
- This is a significant spatial presentation function triggered by the pose offset

## Layer 2: State Object Update (0x140F99C40)

Already documented in `re/RideOnCompletionGates.md`. The function body in order:

1. **Elapsed**: `state+0x180 += delta`
2. **0x381 cleanup**: if bit 0x02 was set, clears it and runs animation cleanup
3. **Animation data**: if `rideRuntime+0x2A0 == 2`, calls `RideRuntime_UpdateSeatAnimationData`
4. **Stage-2 block** (guarded by `state+0x198 == 2`):
   a. Thumbstick input from `rideRuntime+0x732C`
   b. Seat resolve via `sub_1401783C0`
   c. `RideOnState_UpdateSeatPoseRequest`
   d. `RideRuntime_SetRideOnActionSlotFilters`
   e. **Input-reactive pose block** (only when `rideRuntime+0x395` is set):
      - Direction write to `rideRuntime+0x576`
      - `sub_140F4D280(rideOn, 18)` — param id 18 on `rideOn+0x98`
      - Flag `+0x4288` bit update + `PresentationGlobal_WriteActionParam`
   f. Three-gate completion path (see `re/pi/RideOnCompletionGates.md`)

## `RideRuntime_UpdateSeatAnimationData` (0x141012280)

Called from **5 different code sites** — it is a shared animation blend updater:

| Caller | Address | Role |
|---|---|---|
| `DSPlayerVehicleRideOnState_Update` | `0x140F99C40` | Per-frame RideOn update |
| `DSPlayerVehicleRideOnState_ProcessVehicleAttach` | `0x140F9A370` | Attach phase |
| `sub_140F8FDE0` | `0x140F8FDE0` | Drive-state update (writes `plugin+0x11A = 6` on abort) |
| `sub_141081BA0` | `0x141081BA0` | Vehicle state handler |
| `sub_1410D8D40` | `0x1410D8D40` | Vehicle state handler |

Function logic:
- Resolves two seat families: `word_1444BDB10` and `word_1444BAD20`
- For `word_1444BDB10` match: reads/writes `seat+0x121C` (seat pose flag)
- For `word_1444BAD20` match with action tree: 
  - Checks slot 35 via `ActionTree_FindSlotByByteId`
  - If slot 35 has a clip: `seat+0x1268 = 1060911113` (~0.6f)
  - If flag `0x20000000` in seat state + slot 36 found:
    - Reads slot 36 param, vmaxss with `rideRuntime+0x24488` → `seat+0x1268`
  - Otherwise: `sub_140DBF000` time query + threshold math → `seat+0x1268`
- Cleans up: `rideRuntime+0x24488 = 0`

## Tiny Wrapper (0x140F970D0)

A 0x26-byte function:
```
sub_14100FBE0(rideRuntime)     // seat entity signal
sub_14100FAD0(rideRuntime)     // seat baggage update
```
This wrapper is a vtable slot used outside the main dispatcher path.

## Key Helper Functions Discovered

| Address | Name/Role |
|---|---|
| `0x140F4D270` | Single-byte flag setter: `*(a1+10) = 1` (transition trigger) |
| `0x140F4D280` | Param writer: `rideOn+0x98` array `[6*idx + 0x3410] = val`, flag `[48*idx + 0x3400] \|= 4` |
| `0x140138810` | Recursive seat entity signal: sets `obj+0x557 = val`, broadcasts to children under SRW lock |
| `0x141CBBAC0` | Object resolver by hash type/id; case 0 resolves `word_144281A90` objects |
| `0x1413A90F0` | Event broadcast system; dispatches to action graph listeners via `sub_1402965C0`/`sub_140295C70` |
| `0x141013EB0` | Sets `rideRuntime+0x395 = 1`; called from Drive-state update and `sub_140F97E60` |

## Timing Breakdown

Verified from runtime trace (automated trike boarding):

| Phase | Elapsed (s) | Event |
|---|---|---|
| OnEnter | 0 | Parameter envelope + AnimSetState(5) |
| Attach call 1 | 0.017 | ProcessVehicleAttach: stage 0→1, b18A=1 |
| Attach call 2 | 0.029 | ProcessVehicleAttach: stage 1→2, b189=1, b381=0x2 |
| Update | 0.046 | Clears b381, SeatPoseRequest + SlotFilters run |
| **3.55 s gap** | — | Per-frame animation from both Layer 1 and Layer 2 |
| Attach call 3 | 3.262 | ProcessVehicleAttach: b18B=1 |
| Update gate 1 | 3.595-3.612 | `sub_140DBEA00(player, 0xED)` → true → `next=2` |
| DriveEnter | 3.612 | RideOn→Drive transition |

## What The Current Fast-Drive Hook Skips

The trace ASI hooks `DSPlayerVehicleRideOnState_Update`, lets original run, then
when `(current==1 && next==1 && stage==2 && b18A && b191)` writes `next=2`.

This bypasses:
1. The 3.55 s per-frame animation loop (dispatcher cmd 6 + Update input-reactive block)
2. The `ActionParams_QueryBoolByParamId(0xED)` gate (gate 1)
3. The timer threshold comparison (gate 3)
4. The event construction and broadcast (`sub_141CBBAC0` + `sub_1413A90F0`)

These are all presentation/cosmetic — the event broadcast notifies action graph
listeners of "boarding complete" but the Drive state transition works without it.

## Remaining Artifact

A short pose/position settling artifact (~fraction of a second) remains because:
- Blend values in `seat+0x1268` normally accumulate over 3.6 s of per-frame updates
- Inner animation track slots have not played through their blend-in period
- Transitional clips may not have played before the seated idle clip

Eliminating this artifact would require manually seeding all blend targets to
their Drive-state values — a larger reverse-engineering scope.

## Minimum Achievable Latency

~0.05 s is the lower bound while preserving full seat pose initialization:
- ~0.017 s: ProcessVehicleAttach × 1 (stage 0→1)
- ~0.029 s: ProcessVehicleAttach × 2 (stage 1→2)  
- ~0.046 s: First Update with stage==2 (pose/filter pass)
- ~0.050 s: Fast Drive request at next frame
