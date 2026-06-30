# RideOn Completion Gates (Verified Static)

Scope: precise static control flow of `DSPlayerVehicleRideOnState_Update` (`0x140F99C40`)
leading to the Drive (state 2) request, plus the early-finish (state 3) blocker chain and the
`rideRuntime + 0x18B` set path.

All facts below are verified directly from IDA disassembly / decompilation. No runtime hook
of the gates themselves has been run yet; runtime correlations cited here come from the
existing read-only observation runs already recorded in
`RE/PassengerCargoVehicleMountAnalysis.md`.

Object pointer conventions used below:
- `state` = the RideOnState object (= `plugin + 0x150`). It is the `rcx`/`rbx`/`rdi` argument
  of the functions in this file.
- `plugin` = `DSPlayerRideVehicleActionPlugin` = `*(qword *)(state + 0x88)`.
- `rideRuntime` = `*(qword *)(state + 0x190)`.
- `playerEntity` = `*(qword *)(state + 0x98)`.
- Field offsets on `state`: `+0x88` plugin, `+0x90` obj90, `+0x98` playerEntity, `+0xA0`
  objA0, `+0xA8` vehicle/seat context, `+0x180` elapsed (float), `+0x190` rideRuntime,
  `+0x198` stage (dword), `+0x19C` flag19C (byte).
- Field offsets on `rideRuntime`: `+0x189`, `+0x18A`, `+0x18B` stage-2 flag, `+0x190` b190,
  `+0x191` b191, `+0x192` b192, `+0x2A0` rideMode dword, `+0x37B`, `+0x37D` (bit 4),
  `+0x381` (bits), `+0x3B0`.

## Update entry

`0x140F99C48`: `vaddss xmm0, xmm1, dword ptr [rcx+180h]` then store back to `state + 0x180`.
This accumulates the frame delta into the RideOn elapsed timer at `state + 0x180`.

`0x140F99C5B`-`0x140F99D3C`: if `rideRuntime + 0x381` bit `0x02` is set, the bit is cleared
and a short animation/seat-flag update runs. `rideRuntime + 0x381` bit `0x02` is the bit set
by `ProcessVehicleAttach` stage 1 success (`0x140F9AC60`).

`0x140F99D4A`: if `*(dword *)(rideRuntime + 0x2A0) == 2`, calls
`RideRuntime_UpdateSeatAnimationData` (`0x141012280`).

`0x140F99D51`: the entire completion-relevant block runs only when
`*(dword *)(state + 0x198) == 2` (the ProcessVehicleAttach internal stage == 2). Runtime
confirmed `state + 0x198` reaches `2` early in RideOn and stays `2`.

## The three completion gates to state 2

The single instruction that requests Drive is:

```
0x140F9A2C7: mov word ptr [rax+11Ah], 2   ; *(word *)(plugin + 0x11A) = 2  (next state = Drive)
```

where `rax = *(qword *)(state + 0x88)` = `plugin`. This write is reachable only through the
block at `0x140F9A0B7`. `0x140F9A0B7` has exactly three predecessors, which are the three
completion gates. Each gate is purely data/state driven (no register-context dependency).

### Gate 1 — player entity query `sub_140DBEA00(playerEntity, 0xED)`

```
0x140F99FC0: mov rcx, [rbx+98h]      ; rcx = playerEntity
0x140F99FC7: mov edx, 0EDh           ; query id = 0xED (237)
0x140F99FCC: call sub_140DBEA00
0x140F99FD1: test al, al
0x140F99FD3: jnz  loc_140F9A0B7      ; if query returns nonzero -> completion
```

If the query returns nonzero, control jumps directly to the completion block, skipping gates
2 and 3 entirely.

`sub_140DBEA00` (`0x140DBEA00`) is a two-level virtual dispatch:

```c
char sub_140DBEA00(_QWORD *entity, unsigned int id)
{
    if ( (*(unsigned int (*)(...))(*entity + 0xA0))(entity) == 0xFFFF )
        return 0;                       // entity vtable+0xA0(entity) must not be 0xFFFF
    comp = entity[277];                 // *(qword *)(entity + 0x8A8)  (entity sub-component)
    fn  = *(...)(*(qword *)comp + 0xE0); // comp vtable+0xE0
    v   = (*(unsigned int (*)(...))(*entity + 0xA0))(entity, id); // entity vtable+0xA0(entity, id)
    return (char)fn(comp, v, 0);        // comp vtable+0xE0(comp, v, 0)
}
```

So the result is determined by the player entity's own vtable (`+0xA0`) and the
`playerEntity + 0x8A8` sub-component's vtable (`+0xE0`), parameterised by id `0xED`. This is
polymorphic and fully data/state driven. The concrete vtable targets are not resolved here
(see open-questions doc). Direct entry-hook verification of this gate is infeasible:
`EntityComponent_QueryBoolById` has 90+ callers and is a hot generic query whose detour
crashes the game during save load (see open-questions doc).

### Gate 2 — `rideRuntime + 0x190` (b190) and `rideRuntime + 0x192` (b192) both nonzero

```
0x140F99FD9: mov rcx, [rbx+190h]      ; rcx = rideRuntime
0x140F99FE0: cmp [rcx+190h], al       ; al == 0 here (gate 1 returned 0)
0x140F99FE6: jz  short loc_140F99FF4  ; if b190 == 0 -> go to gate 3 (timer)
0x140F99FE8: cmp [rcx+192h], al       ; b192 vs 0
0x140F99FEE: jnz loc_140F9A0B7        ; if b192 != 0 -> completion
```

So gate 2 fires only when `b190 != 0 AND b192 != 0`. If either is zero, control falls through
to gate 3. Runtime showed `b190 = 0` and `b192 = 0` throughout normal player boarding, so gate
2 did not fire in the observed run.

### Gate 3 — elapsed timer >= `dword_143461CCC` (= 5.0 seconds)

```
0x140F99FF4: vmovss xmm0, dword ptr [rbx+180h]   ; xmm0 = elapsed (state + 0x180)
0x140F99FFC: vcomiss xmm0, cs:dword_143461CCC    ; compare elapsed vs 5.0f
0x140F9A004: jnb  loc_140F9A0B7                   ; if elapsed >= 5.0f -> completion
```

`dword_143461CCC` raw `0x40A00000`, confirmed `5.0f` via `int_convert` / `struct.unpack`.
Runtime showed the Drive request fired at `elapsed = 3.61`, which is below `5.0`, so gate 3
did not fire in the observed run.

## Early-finish blocker chain (state 3, never state 2)

If all three gates fail (gate 1 returns 0, AND (b190 == 0 OR b192 == 0), AND elapsed < 5.0),
control falls through to `0x140F9A00A`. This chain can only write `plugin + 0x11A = 3`
(RideOff early-finish) or exit without changing `next`. It never writes state 2.

```
0x140F9A00A: call RideVehicleRuntime_CheckAbortAndRequestState5
0x140F9A00F: test al, al
0x140F9A011: jnz  loc_140F9A34B                  ; abort -> exit (state 5 written inside helper)

0x140F9A017: mov rax, [rbx+90h]                  ; state + 0x90
0x140F9A01E: mov rcx, [rax+2A8h]
0x140F9A025: test rcx, rcx
0x140F9A028: jz   short loc_140F9A037
0x140F9A02A: test byte ptr [rcx+5D0h], 4         ; bit 0x04 of (state+0x90)->0x2A8->0x5D0
0x140F9A031: jnz  loc_140F9A34B                  ; set -> exit

0x140F9A037: mov rcx, [rbx+190h]                 ; rideRuntime
0x140F9A03E: test byte ptr [rcx+37Dh], 4         ; rideRuntime + 0x37D bit 0x04
0x140F9A045: jnz  loc_140F9A34B                  ; set -> exit

0x140F9A04B: call DSPlayerVehicleRideOnState_CanEarlyFinishRideOn
0x140F9A050: test al, al
0x140F9A052: jz   loc_140F9A34B                  ; false -> exit

0x140F9A058: mov rax, [rbx+190h]                 ; rideRuntime
0x140F9A05F: cmp byte ptr [rax+18Bh], 0          ; rideRuntime + 0x18B vs 0
0x140F9A066: jz   loc_140F9A34B                  ; 0x18B == 0 -> exit

0x140F9A06C: mov rax, cs:qword_14623E3C0
0x140F9A073: mov rcx, 40000000000000h
0x140F9A07D: test [rax+160h], rcx                ; bit 0x40000000000000 of qword_14623E3C0+0x160
0x140F9A084: jnz  loc_140F9A34B                  ; set -> exit

0x140F9A08A: cmp cs:qword_14623E908, 0
0x140F9A092: jz   short loc_140F9A0A2
0x140F9A094: vxorps xmm2, xmm2, xmm2
0x140F9A098: mov edx, 0FFFFFFFFh
0x140F9A09D: call sub_140E21970

0x140F9A0A2: mov rax, [rbx+88h]                  ; plugin
0x140F9A0A9: mov word ptr [rax+11Ah], 3          ; *(word *)(plugin + 0x11A) = 3 (RideOff)
0x140F9A0B2: jmp loc_140F9A34B                   ; exit
```

Conclusion: `rideRuntime + 0x18B` is a precondition only for the early-finish (state 3) chain.
It is NOT a gate for the normal completion (state 2) path. The runtime observation that
`0x18B` became `1` before `next = 2` is a correlation (both occur after stage 2 progresses),
not a causal gate for Drive.

## `rideRuntime + 0x18B` set path (ProcessVehicleAttach stage 2)

`DSPlayerVehicleRideOnState_ProcessVehicleAttach` (`0x140F9A370`). Stage dispatch at
`0x140F9A49C` on `*(dword *)(state + 0x198)`:

- stage 0 -> `0x140F9AC76` (contains the attach path leading to `Entity_AttachToParentAndNotify`
  at `0x140F9AD80`)
- stage 1 -> `0x140F9A547`
- stage 2 -> fall through to `0x140F9A4BC` (the `0x18B` setter path)
- stage > 2 -> exit

The stage 2 setter block (`0x140F9A4BC`-`0x140F9A542`) requires ALL of:

```
0x140F9A4BC: cmp [rdi+19Ch], r12b      ; state + 0x19C must be 0
0x140F9A4C3: jnz loc_140F9AFF8         ; else exit
0x140F9A4C9: mov rax, [rdi+0A0h]       ; state + 0xA0
0x140F9A4D0: mov ecx, [rax+7378h]      ; (state+0xA0) + 0x7378
0x140F9A4D6: shr rcx, 18h              ; shift right 0x18 (24)
0x140F9A4DA: test cl, 1                ; bit 0 of ((value) >> 24)
0x140F9A4DD: jz  loc_140F9AFF8         ; bit clear -> exit
0x140F9A4E3: mov rax, [rdi+190h]       ; rideRuntime
0x140F9A4EA: cmp [rax+18Bh], r12b      ; rideRuntime + 0x18B must be 0 (not yet set)
0x140F9A4F1: jnz loc_140F9AFF8         ; already set -> exit
```

When all hold:

```
0x140F9A4F7: vpxor xmm0, xmm0, xmm0
0x140F9A520: call sub_142071F60        ; stack-buffer setup
0x140F9A525: xor r8d, r8d              ; r8 = 0
0x140F9A528: lea rcx, [rsp+...]        ; rcx = stack buffer
0x140F9A52D: mov dl, 1                 ; dl = 1
0x140F9A52F: call sub_140F8E8D0        ; sub_140F8E8D0(buf, 1, 0)
0x140F9A534: mov rax, [rdi+190h]       ; rideRuntime
0x140F9A53B: mov byte ptr [rax+18Bh], 1; rideRuntime + 0x18B = 1
0x140F9A542: jmp loc_140F9AFF8         ; exit (stage unchanged)
```

This path does NOT call `Entity_AttachToParentAndNotify`. The attach call (`0x140F9AD80`) is
in the stage 0 handler. `rideRuntime + 0x18B = 1` is a stage-2 flag set after the pre-attach
helper `sub_140F8E8D0`, not the attach result.

Stage 1 success (block `0x140F9AC4B`-`0x140F9AC76`) writes, in order:

```
0x140F9AC52: mov byte ptr [rax+189h], 1   ; rideRuntime + 0x189 = 1
0x140F9AC59: mov rax, [rdi+190h]
0x140F9AC60: or   byte ptr [rax+381h], 2  ; rideRuntime + 0x381 bit 0x02
0x140F9AC67: mov dword ptr [rdi+198h], 2  ; state + 0x198 = 2 (advance stage to 2)
0x140F9AC71: jmp loc_140F9AFF8
```

This matches the runtime observation: a `ProcessVehicleAttach` call moved stage `1 -> 2`,
set `rideRuntime + 0x189 = 1`, and temporarily set `rideRuntime + 0x381` bit `0x02`
(cleared again by the next `Update`).

## `DSPlayerVehicleRideOnState_CanEarlyFinishRideOn` (`0x141011480`)

This is a gate for the early-finish (state 3) chain only, not for state 2. It returns true
(allow early finish) when all of the following hold:

1. `*(byte *)(qword_14623E8C8 + 2254012)` nonzero -> immediate `return 1`.
2. Object chain: `v3 = *(qword *)(state[34] + 0xC8)`; if `*(byte *)(v3 + 8)` is nonzero,
   then `v5 = *(qword *)(v3 + 0x148)`; if `v5 != 0x20`, `sub_141FAAC20()` must return 0,
   else `return 0`.
3. `*(float *)(state + 0x24590)` compared against `0.0` (the branch is combined with the
   `v4` flag from step 2; if `v4` is false, `return 0`).
4. `(*(byte (**)(...))(*state + 0xC8))(state, 168, stackBuf)` must return nonzero, else
   `return 0`.
5. `(*(byte *)(qword_14623E3C0 + 0x160) & 0x40)` must be clear, else `return 0`.
6. `v13 = state[5]` (= `*(qword *)(state + 0x28)`); return
   `!sub_140F651B0(v13, 168, 158) || !sub_140D7F290(v13, 158)`.

`dword_1434623F8` used inside this function is `1000000.0f`.

## OnEnter / OnExit lifecycle of b190 / b191 / b192

`DSPlayerVehicleRideOnState_OnEnter` (`0x140F98CE0`):

- `*(dword *)(state + 0x198) = 0` (reset stage)
- `*(dword *)(state + 0x180) = 0` (reset elapsed)
- `*(byte *)(rideRuntime + 0x3B0) = 0`
- `*(byte *)(rideRuntime + 0x37B) = 1`
- `*(byte *)(rideRuntime + 0x381) &= ~2` (clear bit 0x02)
- `*(byte *)(state + 0x19C) = 1` when `*(byte *)(state + 0xA8 -> 0x3970) == 3`
- `*(byte *)(rideRuntime + 0x190) = 1` (b190) AND `*(byte *)(state + 0x19C) = 1` when
  `*(byte *)(state + 0xA8 -> 0x3970) == 9`
- `*(byte *)(rideRuntime + 0x191) = 1` (b191) near the end (`0x140F9991A`)

`DSPlayerVehicleRideOnState_OnExit` (`0x140F99990`):

- `*(byte *)(rideRuntime + 0x191) = 0` (clears b191)
- `*(byte *)(rideRuntime + 0x192) = 0` (clears b192)

So b191 is set by OnEnter and cleared by OnExit (runtime confirmed: `1` during
RideOn/Drive/RideOff, cleared at the end). b190 is set by OnEnter only for the
`state + 0xA8 -> 0x3970 == 9` boarding context. b192 is cleared by OnExit but its setter is
not in OnEnter, OnExit, `RideRuntime_UpdateBaggageEventAndSeatFlags`, or
`RideOnState_Update`; runtime showed b192 = 0 throughout normal boarding.

## Corrections to prior unverified notes

`RE/UnverifiedRideOnStaticNotes.md` previously guessed that the pre-timer blocker chain could
lead to the state 2 write. The disassembly above shows the blocker chain only writes state 3
or exits; state 2 is reachable only via `0x140F9A0B7`, whose three predecessors are the three
gates listed above. The prior note's "blocker chain -> state 2" implication is incorrect.

The prior note's timer direction is correct: `jnb 0x140F9A0B7` means `elapsed >= 5.0` goes to
completion; `elapsed < 5.0` falls through to the blocker chain.

The prior note's `0x18B` set location (`0x140F9A53B`) and the `sub_140F8E8D0(buf, 1, 0)`
call are confirmed; the additional preconditions (`state + 0x19C == 0`, bit 24 of
`(state + 0xA0) + 0x7378`, and `0x18B == 0`) are newly documented here.
