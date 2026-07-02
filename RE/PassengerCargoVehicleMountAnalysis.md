# Passenger/Cargo Vehicle Mount Analysis

## Scope

Goal: identify the game logic that mounts passenger or human-cargo style entities into a vehicle without the player ride-on animation.

This analysis is based on targeted IDA MCP decompilation and xrefs only. No global text search was used.

## Confirmed Mount Chain

### 1. MountableComponent export registration

`MountableComponent_RegisterMountExport` at `0x1402F78E0` registers:

- symbol set: `MountableComponentSymbols`
- exported function: `MountableComponent_sExportedMount`
- script/export name: `Mount`
- implementation: `MountableComponent_ExportedMount_Thunk` at `0x1402F88A0`

`MountableComponent_ExportedMount_Thunk` is a thunk to `MountableComponent_ExportedMount_SelectSlot` at `0x1402F38B0`.

### 2. Exported Mount selects a compatible slot

`MountableComponent_ExportedMount_SelectSlot` (`0x1402F38B0`) receives a mountable side object, a mounter/passenger side object, a transform or target descriptor, and a flag.

Observed behavior:

- collects mountable candidates via `sub_1402F76A0`
- finds the compatible mount point by matching entries from the mountable point table
- checks whether the mounter is already mounted through `sub_14011FFA0(..., word_1441FE060)`
- computes a compatible mount point and slot table using `sub_1402F3BB0` and `sub_1402F4010`
- chooses the nearest slot transform by distance to the mounter position
- calls:

```cpp
MountableComponent_StartMount(mountable, mounter, mountPoint, slotTransform, flag);
```

Call site: `0x1402F3B9C`.

### 3. StartMount stores mount state and attaches the mounter

`MountableComponent_StartMount` (`0x1402F1EF0`) is the core direct mount function.

Observed writes:

```cpp
*(byte *)(mountable + 304) = flag;
*(word *)(mountable + 80) = 1;
*(qword *)(mountable + 288) = mountPoint;
*(qword *)(mountable + 296) = slotTransform;
*(qword *)(mountable + 88) = mounter;
```

If the mountable owner is ready, it immediately calls:

```cpp
Entity_AttachToParentAndNotify(mounter, mountableOwner, 0);
```

Call site: `0x1402F1F43`.

If not ready, it schedules a callback through `sub_14011D850`; the callback at `0x1402F2040` later calls `Entity_AttachToParentAndNotify(mounter)`.

### 4. Entity attach function sends parent/camera messages

`Entity_AttachToParentAndNotify` (`0x140130900`) is a generic entity parent/attach update routine.

Confirmed effects:

- updates entity parent relationship
- emits `MsgParentChanged`
- emits `MsgSetCameraVisibility`
- updates internal linked structures under SRW lock

This is the shared parent/attach update routine observed in the direct passenger/cargo chain and in later player RideOn handling.

## Passenger/Human-Cargo Candidate Chain

### Update gate

`PassengerCargo_UpdateMaybeVehicleMount` (`0x1408E2CD0`) is the only direct code caller of `PassengerCargo_SelectSlotAndStartMount` (`0x1408E6F50`).

The relevant gate is:

```cpp
if (*(qword *)(a1 + 48) + 752 is valid
    && state in [2..4]
    && *(byte *)(a1 + 728) == 0
    && *(qword *)(a1 + 512) != 0
    && *(byte *)(a1 + 681) != 0
    && PassengerCargo_CanStartVehicleMount(a1))
{
    PassengerCargo_SelectSlotAndStartMount(a1, ...);
}
```

Call site: `0x1408E318B`.

`a1 + 728` is set after successful mount and prevents repeated auto-mount.

### CanStartVehicleMount

`PassengerCargo_CanStartVehicleMount` (`0x1408E6A90`) rejects mounting when:

- the carried/mounted entity at `a1 + 512` already has an occupied pointer at `+96`
- `sub_140171740(entity)` reports a blocking state
- components found by UUID-like refs `unk_1442D74C0` or `unk_1442E6430` report blocking bytes
- entity flags at `+152` include `0x100`
- configured constraints or a spatial query fail

The successful path performs an OBB/spatial query through `sub_142450CF0` and confirms the target entity appears in the query result.

### SelectSlotAndStartMount

`PassengerCargo_SelectSlotAndStartMount` (`0x1408E6F50`) performs the actual passenger/cargo auto-mount:

- resolves the vehicle/mountable side object
- enumerates mountable candidates via `sub_1402F76A0`
- matches the configured mount point descriptor
- resolves a compatible seat/slot through the same mount point tables used by `MountableComponent_ExportedMount_SelectSlot`
- performs nav/path related checks using `AITerrainCosts` and `NavMeshCostFunction`
- calls:

```cpp
MountableComponent_StartMount(selectedMountable, actorEntity, selectedMountPoint, selectedSlotTransform, 0);
```

Final call site: `0x1408E7C89`.

After the call:

```cpp
*(byte *)(a1 + 728) = 1;
```

## Relation To Player Boarding

Player boarding currently follows the ride action plugin path:

```text
DSPlayerRideVehicleActionPlugin -> RideOnState -> ride-on animation
```

`DSPlayerVehicleRideOnState::OnEnter` at `0x140F98CE0` was checked after the passenger/cargo chain was identified.

Confirmed player RideOn OnEnter behavior:

- does not call `MountableComponent_StartMount` (`0x1402F1EF0`)
- does not call `Entity_AttachToParentAndNotify` (`0x140130900`)
- sets vehicle and player ride/animation state flags
- resolves vehicle/seat related data through `sub_1401783C0` and `sub_140157460`
- sends `MsgDsBaggageEvent` through `sub_140130C60`
- calls `sub_140E21970` near the end of RideOn initialization

The passenger/cargo chain above bypasses `DSPlayerVehicleRideOnState::OnEnter`. It uses `MountableComponent_StartMount` and `Entity_AttachToParentAndNotify` directly, then marks the passenger/cargo state object as mounted.

Additional player RideOn handling:

- `DSPlayerVehicleRideOnState_ProcessVehicleAttach` (`0x140F9A370`) is present in `DSPlayerVehicleRideOnState` vtable slot `[27]` (`0x14325B808`).
- `sub_14101CD90` dispatch case `6` jumps to vtable offset `+0xD8`; for `DSPlayerVehicleRideOnState`, that target is `DSPlayerVehicleRideOnState_ProcessVehicleAttach`.
- `DSPlayerVehicleRideOnState_ProcessVehicleAttach` resolves the same `word_1444BCA50` vehicle/seat object from `[state + 0x190] + 0x220` through `sub_1401783C0`.
- Before the attach call, it calls `sub_141F6BDC0` with `a3 = 1` and `a5 = 0`.
- On success, it calls `Entity_AttachToParentAndNotify` at `0x140F9AD80` with `rcx = [state + 0x98]`, `rdx = resolved vehicle/seat object`, and `r8 = 0`.

The confirmed shared primitive between the passenger/cargo chain and player RideOn handling is `Entity_AttachToParentAndNotify`, not `MountableComponent_StartMount`.

```text
PassengerCargo_UpdateMaybeVehicleMount
  -> PassengerCargo_CanStartVehicleMount
  -> PassengerCargo_SelectSlotAndStartMount
  -> MountableComponent_StartMount
  -> Entity_AttachToParentAndNotify
```

2026-07-02 direct-attach experiment update:

- A player experiment skipped the original `RideOnState_OnEnter` body and manually
  called the original `ProcessVehicleAttach` trampoline twice from the OnEnter hook.
- That path reached the player `ProcessVehicleAttach -> Entity_AttachToParentAndNotify`
  chain and then requested Drive by writing `plugin + 0x11A = 2`.
- Runtime reached `RideOnExit` and `DriveEnter` in the same frame
  (`elapsed=0.0166834` in the automated screenshot run), instead of waiting for
  the normal RideOn timer.
- Screenshots showed the long climb sequence was removed, but a short pose/position
  settling artifact remains before the player is fully seated.
- This strengthens the conclusion that the reusable primitive for the player mod is
  the player `ProcessVehicleAttach -> Entity_AttachToParentAndNotify` chain, not a
  direct call to `MountableComponent_StartMount`.

## Player Path Comparison

Confirmed facts:

- `DSPlayerVehicleRideOnState::OnEnter` (`0x140F98CE0`) does not call `MountableComponent_StartMount` (`0x1402F1EF0`).
- `DSPlayerVehicleRideOnState::OnEnter` (`0x140F98CE0`) does not call `Entity_AttachToParentAndNotify` (`0x140130900`).
- `DSPlayerVehicleRideOnState_ProcessVehicleAttach` (`0x140F9A370`) does call `Entity_AttachToParentAndNotify` (`0x140130900`) at `0x140F9AD80`.
- No code xref from the player RideVehicle range `0x140F80000..0x141020000` to `MountableComponent_StartMount` (`0x1402F1EF0`) was found by targeted xref filtering.
- `DSPlayerVehicleRideOnState::OnEnter` already resolves the target vehicle/seat data and writes player/vehicle ride animation flags.
- `DSPlayerVehicleRideOnState::OnExit` (`0x140F99990`) resolves the same vehicle/seat object and calls `sub_140F8EA80`.
- `sub_140F8EA80` only sets a word flag at target object `+2352` under a critical section, then releases the reference. It is a player ride/seat cleanup flag write, not the inverse of `MountableComponent_StartMount`.

## IDA Changes Made

Renamed functions:

- `0x1402F1EF0` -> `MountableComponent_StartMount`
- `0x1402F38B0` -> `MountableComponent_ExportedMount_SelectSlot`
- `0x1402F88A0` -> `MountableComponent_ExportedMount_Thunk`
- `0x1402F78E0` -> `MountableComponent_RegisterMountExport`
- `0x140130900` -> `Entity_AttachToParentAndNotify`
- `0x1408E2CD0` -> `PassengerCargo_UpdateMaybeVehicleMount`
- `0x1408E6A90` -> `PassengerCargo_CanStartVehicleMount`
- `0x1408E6F50` -> `PassengerCargo_SelectSlotAndStartMount`
- `0x140F9A370` -> `DSPlayerVehicleRideOnState_ProcessVehicleAttach`

Added comments at the confirmed call sites and state writes listed above.

## Runtime Verification

2026-06-29 automated boarding script run:

- The script completed the game launch, intro skip, continue, recover-confirm guard keys, board, dismount, and quit sequence.
- The run installed the `MountableComponent_StartMount` hook at plugin initialization.
- During the player board/dismount sequence, the log did not contain any `StartMount:` runtime call entry.
- The player board action produced one ride action plugin init transition:

```text
[21:44:26.857][VehicleBoard] Init: plugin=0x2AD6DCF0100 result=1 current=0 next=1 flag=0
```

This runtime check matches the static finding that the player RideOn path does not use `MountableComponent_StartMount`.

2026-06-29 follow-up run after trace log filtering:

- The script again completed the launch, board, dismount, and quit sequence.
- The filtered trace removed the repeated zero-state `Init` entries.
- The run still did not contain any `StartMount:` runtime call entry during player board/dismount.
- The player board action again produced one ride action plugin init transition:

```text
[21:48:08.462][VehicleBoard] Init: plugin=0x226B4CF0100 result=1 current=0 next=1 flag=0
```

2026-06-29 player attach callsite hook run:

- A narrow callsite hook at `0x140F9AD80` completed without crashing.
- The hook confirmed that the player RideOn path reaches the exact `Entity_AttachToParentAndNotify` callsite identified statically.
- Runtime parameters at the callsite:

```text
[21:52:57.381][VehicleBoard] Init: plugin=0x56F1FCF0100 result=1 current=0 next=1 flag=0
[21:52:57.383][VehicleBoard] PlayerAttachCall: player=0x56F07948000 vehicleSeat=0x56F0B100000 arg=0x0
```

2026-06-29 player attach callsite hook with plugin state:

- At the attach callsite, the latest RideOn action plugin had already entered state `current=1`.
- The plugin state bytes at attach time were:

```text
[21:56:04.989][VehicleBoard] Init: plugin=0x200724F0100 result=1 current=0 next=1 flag=0
[21:56:04.991][VehicleBoard] PlayerAttachCall: player=0x2005A16C000 vehicleSeat=0x2005E970000 arg=0x0 plugin=0x200724F0100 current=1 next=1 flag=0
```

## RideVehicle State Table

Confirmed static facts from IDA:

- `DSPlayerRideVehicleActionPlugin_DispatchStateTransition` (`0x14111F920`) reads:
  - `plugin + 0x118`: current state byte
  - `plugin + 0x11A`: next state byte
  - `plugin + 0x11B`: state flag byte
- If `*(word *)(plugin + 0x11A)` differs from the current state byte, it calls the plugin vtable transition handler at offset `+0xB8`.
- After the transition handler returns, it writes `current = next` and clears `flag = 0`.
- `DSPlayerRideVehicleActionPlugin_ApplyStateTransition` (`0x140FE4560`) calls the current handler stored at `plugin + 0x140` with command `1`, then loads a new handler from `funcs_140FE45AA[next]`, stores it to `plugin + 0x140`, and calls the new handler with command `0`.
- `funcs_140FE45AA` is at `0x142E0C9D0` in `.rdata`.
- Confirmed handler thunks:
  - state `1`: `0x14100AF00`, loads `plugin + 0x150` (`RideOnState`) and tail-calls vtable slot `[1]`
  - state `2`: `0x14100AF10`, loads `plugin + 0x158` (`DriveState`) and tail-calls vtable slot `[1]`
  - state `3`: `0x14100AF20`, loads `plugin + 0x160` (`RideOffState`) and tail-calls vtable slot `[1]`
- `DriveState` vtable slot `[1]` is the same command dispatcher `sub_14101CD90`.
- `DriveState` command `0` reaches `DSPlayerVehicleDriveState_OnEnter` (`0x140F8EB40`) through vtable offset `+0x58`.
- `DSPlayerVehicleDriveState_OnEnter` resolves the vehicle object and calls `sub_141F6BDC0`, then writes drive/input/control state fields.
- `DSPlayerVehicleDriveState_OnEnter` does not contain a direct call to `Entity_AttachToParentAndNotify` in the decompiler output.

No runtime validation has been made for changing this table.

2026-06-29 runtime check for RideOn timer constant:

- `RideOnState` update function `0x140F99C40` accumulates elapsed time at `state + 0x180`.
- The function compares that elapsed time with `dword_143461CCC` before one path that eventually writes `*(word *)(plugin + 0x11A) = 2` at `0x140F9A2C7`.
- Runtime patch tested: write `0` to `base + 0x3461CCC` (`dword_143461CCC`, original raw value `0x40A00000`).
- The patch was applied successfully at startup:

```text
[22:15:22.372][VehicleBoard] RideOn time patch addr=0x7FF6ECD81CCC old=0x40A00000 new=0x0
```

- The game exited before entering the loaded save flow, during the automated script's recover prompt phase.
- The patch was removed and the trace ASI was rebuilt as a no-op installer.

2026-06-29 read-only RideVehicle Init observation hook run:

- The trace ASI installed a read-only observation hook on `DSPlayerRideVehicleActionPlugin_Init` and did not modify the function result or RideVehicle state fields.
- The automated boarding script completed launch, board, dismount, and quit without crash.
- The run captured one successful RideVehicle init:

```text
[22:42:11.701][VehicleBoard] Init result=1 plugin=0x5FDFA0F0100
```

- Runtime polling of that plugin confirmed `plugin + 0x150` points to the RideOn state object and `RideOnState + 0x190` points back to the same runtime/plugin object.
- Observed RideOn field sequence during player boarding:

```text
cur=1 next=1 stage=1 elapsed=0.0333667 b189=0 b18A=1 b18B=0 b190=0 b191=1 b192=0 b381=0x0
cur=1 next=1 stage=2 elapsed=0.0792459 b189=1 b18A=1 b18B=0 b190=0 b191=1 b192=0 b381=0x0
cur=1 next=1 stage=2 elapsed=3.29913   b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x0
cur=2 next=2 stage=2 elapsed=3.61611   b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x4
cur=3 next=3 stage=2 elapsed=3.61611   b189=1 b18A=0 b18B=0 b190=0 b191=1 b192=0 b381=0x0
cur=3 next=3 stage=2 elapsed=3.61611   b189=1 b18A=0 b18B=0 b190=0 b191=0 b192=0 b381=0x0
cur=0 next=0 stage=2 elapsed=3.61611   b189=0 b18A=0 b18B=0 b190=0 b191=0 b192=0 b381=0x0
```

- Runtime result: during this board/dismount run, `rideRuntime + 0x190` and `rideRuntime + 0x192` stayed `0`.
- Runtime result: `rideRuntime + 0x191` was `1` during RideOn/Drive/RideOff and later cleared to `0`.
- Runtime result: `state + 0x198` moved from `1` to `2` early during RideOn and remained `2` through the sampled Drive/RideOff sequence.
- Runtime result: `rideRuntime + 0x18B` changed from `0` to `1` before the sampled Drive transition (`cur=2 next=2`) and was later cleared during RideOff.

2026-06-29 read-only `ProcessVehicleAttach` observation hook run:

- The trace ASI installed read-only observation hooks on `DSPlayerRideVehicleActionPlugin_Init` and `DSPlayerVehicleRideOnState_ProcessVehicleAttach`.
- The automated boarding script completed launch, board, dismount, and quit without crash.
- The run captured a successful RideVehicle init:

```text
[22:49:17.263][VehicleBoard] Init result=1 plugin=0x3366D4F0100
```

- The first observed `ProcessVehicleAttach` call changed `state + 0x198` from `0` to `1` and changed `rideRuntime + 0x18A` from `0` to `1`:

```text
Attach entry cur=1 next=1 stage=0 elapsed=0.0166834 b189=0 b18A=0 b18B=0 b190=0 b191=1 b192=0 b381=0x0
Attach exit  cur=1 next=1 stage=1 elapsed=0.0166834 b189=0 b18A=1 b18B=0 b190=0 b191=1 b192=0 b381=0x0
```

- The next observed `ProcessVehicleAttach` call changed `state + 0x198` from `1` to `2`, changed `rideRuntime + 0x189` from `0` to `1`, and temporarily set `rideRuntime + 0x381` to `0x2`:

```text
Attach entry cur=1 next=1 stage=1 elapsed=0.0291959 b189=0 b18A=1 b18B=0 b190=0 b191=1 b192=0 b381=0x0
Attach exit  cur=1 next=1 stage=2 elapsed=0.0291959 b189=1 b18A=1 b18B=0 b190=0 b191=1 b192=0 b381=0x2
```

- A later `ProcessVehicleAttach` call changed `rideRuntime + 0x18B` from `0` to `1` while still in RideOn state:

```text
Attach entry cur=1 next=1 stage=2 elapsed=3.26159 b189=1 b18A=1 b18B=0 b190=0 b191=1 b192=0 b381=0x0
Attach exit  cur=1 next=1 stage=2 elapsed=3.26159 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x0
```

- After `rideRuntime + 0x18B` became `1`, polling observed the transition to Drive:

```text
RidePoll cur=2 next=2 stage=2 elapsed=3.61194 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x4
```

- Runtime result: `ProcessVehicleAttach` is called repeatedly during RideOn before the Drive transition.
- Runtime result: in this run, `rideRuntime + 0x18B` was set by a `ProcessVehicleAttach` call before the observed Drive transition.
- Runtime result: in this run, `rideRuntime + 0x190` and `rideRuntime + 0x192` stayed `0` through the observed RideOn, Drive, and RideOff sequence.

2026-06-29 read-only `RideOnState_Update` observation hook run:

- The trace ASI installed read-only observation hooks on `DSPlayerRideVehicleActionPlugin_Init`, `DSPlayerVehicleRideOnState_ProcessVehicleAttach`, and `DSPlayerVehicleRideOnState_Update`.
- The automated boarding script completed launch, board, dismount, and quit without crash.
- The run captured a successful RideVehicle init:

```text
[22:54:15.732][VehicleBoard] Init result=1 plugin=0x26866CF0100
```

- Early in RideOn, `RideOnState_Update` cleared `rideRuntime + 0x381` from `0x2` to `0x0` after `ProcessVehicleAttach` had moved the stage to `2`:

```text
Update entry cur=1 next=1 stage=2 elapsed=0.0291959 b189=1 b18A=1 b18B=0 b190=0 b191=1 b192=0 b381=0x2
Update exit  cur=1 next=1 stage=2 elapsed=0.0458792 b189=1 b18A=1 b18B=0 b190=0 b191=1 b192=0 b381=0x0
```

- Later in the same RideOn sequence, after `ProcessVehicleAttach` had set `rideRuntime + 0x18B = 1`, `RideOnState_Update` changed `plugin + 0x11A` (`next`) from `1` to `2` while `current` was still `1`:

```text
Update entry cur=1 next=1 stage=2 elapsed=3.59526 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x0
Update exit  cur=1 next=2 stage=2 elapsed=3.61194 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x0
```

- Polling immediately after that update observed the state machine in Drive state:

```text
RidePoll cur=2 next=2 stage=2 elapsed=3.61194 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x4
```

- Runtime result: the normal RideOn-to-Drive request is produced inside `DSPlayerVehicleRideOnState_Update`, by changing `plugin + 0x11A` from `1` to `2`.
- Runtime result: in this run, that update-side `next = 2` write happened after `rideRuntime + 0x18B` was already `1`; `rideRuntime + 0x190` and `rideRuntime + 0x192` were still `0`.
- Runtime result: this run validates function-level behavior for `DSPlayerVehicleRideOnState_Update`; it does not by itself prove which exact instruction inside the function performed the write.

2026-06-29 repeat run for `0x18B -> next=2 -> Drive` order:

- A second automated run completed launch, board, dismount, and quit without crash.
- The run captured a successful RideVehicle init:

```text
[22:58:14.528][VehicleBoard] Init result=1 plugin=0x4598D4F0100
```

- In this second run, `ProcessVehicleAttach` again set `rideRuntime + 0x18B = 1` while the state was still RideOn:

```text
Attach exit cur=1 next=1 stage=2 elapsed=3.26159 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x0
```

- After that, `RideOnState_Update` again changed `plugin + 0x11A` from `1` to `2`:

```text
Update entry cur=1 next=1 stage=2 elapsed=3.59526 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x0
Update exit  cur=1 next=2 stage=2 elapsed=3.61194 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x0
```

- Polling immediately afterward again observed Drive:

```text
RidePoll cur=2 next=2 stage=2 elapsed=3.61194 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x4
```

- Runtime result: across two observed successful runs with the current hooks, the normal player RideOn completion order was `ProcessVehicleAttach` sets `rideRuntime + 0x18B = 1`, then `RideOnState_Update` requests Drive by setting `plugin + 0x11A = 2`, then the state machine reaches Drive.

2026-06-29 read-only `RideVehicleRuntime_CheckAbortAndRequestState5` observation hook run:

- Static IDA reading of `RideVehicleRuntime_CheckAbortAndRequestState5` (`0x141009C50`) shows that its nonzero path writes `*(word *)(plugin + 0x11A) = 5`.
- The trace ASI installed read-only observation hooks on `DSPlayerRideVehicleActionPlugin_Init`, `DSPlayerVehicleRideOnState_Update`, and `RideVehicleRuntime_CheckAbortAndRequestState5`.
- The automated boarding script completed launch, board, dismount, and quit without crash.
- The run captured a successful RideVehicle init:

```text
[23:07:53.016][VehicleBoard] Init result=1 plugin=0x5CCEF0F0100
```

- During normal RideOn before `rideRuntime + 0x18B` became `1`, the abort helper returned `0`:

```text
AbortGate result=0 cur=1 next=1 stage=2 elapsed=0.0500501 b189=1 b18A=1 b18B=0 b190=0 b191=1 b192=0 b381=0x0
```

- During normal RideOn after `rideRuntime + 0x18B` became `1`, the abort helper still returned `0`:

```text
AbortGate result=0 cur=1 next=1 stage=2 elapsed=3.28245 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x0
```

- The same run then observed `RideOnState_Update` request Drive:

```text
Update entry cur=1 next=1 stage=2 elapsed=3.59943 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x0
Update exit  cur=1 next=2 stage=2 elapsed=3.61611 b189=1 b18A=1 b18B=1 b190=0 b191=1 b192=0 b381=0x0
```

- Runtime result: in this normal player boarding run, `RideVehicleRuntime_CheckAbortAndRequestState5` did not request state `5`; it returned `0` both before and after `rideRuntime + 0x18B` became `1`.
