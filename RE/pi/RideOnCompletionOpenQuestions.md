# RideOn Completion — Deductions and Open Questions

Separate from `RideOnCompletionGates.md`. Items here are either (a) logical deductions from
already-verified static + runtime data but not directly hook-verified, or (b) unresolved
static questions, or (c) feasibility analysis of data/state-driven paths. None of this is a
verified implementation scheme.

## Deduction: the observed normal run completed via gate 1

Fact set (all verified):
- Gate 2 requires `rideRuntime + 0x190 != 0 AND rideRuntime + 0x192 != 0`.
- Gate 3 requires `elapsed >= 5.0` (`dword_143461CCC`).
- The blocker chain writes state 3 or exits, never state 2.
- The observed run wrote `plugin + 0x11A = 2` (state 2 / Drive).

Runtime values at the `Update` exit that produced `next = 2` (from
`RE/PassengerCargoVehicleMountAnalysis.md`, 2026-06-29 RideOnState_Update run):

```
Update exit cur=1 next=2 stage=2 elapsed=3.61194 b190=0 b192=0 b18B=1
```

Eliminative reasoning:
- Gate 2: `b190 = 0`, so gate 2 cannot have fired.
- Gate 3: `elapsed = 3.61194 < 5.0`, so gate 3 cannot have fired.
- The result was state 2, which is reachable only through `0x140F9A0B7`.
- The only remaining predecessor of `0x140F9A0B7` is gate 1 (`0x140F99FD3 jnz`).

Therefore `sub_140DBEA00(playerEntity, 0xED)` must have returned nonzero in that update call.

This is a deduction, not a direct observation: no read-only hook on `sub_140DBEA00`'s return
value has been run. A read-only hook on `sub_140DBEA00` (or a callsite observer at
`0x140F99FD1`) would promote this to verified. `sub_140DBEA00`'s entry begins with
`*a1` / vtable dereferences (no RIP-relative global read at the entry), so unlike
`CanEarlyFinishRideOn` it should be compatible with the existing simple trampoline, but this
has not been tested.

Correlation: in the observed run, `rideRuntime + 0x18B` became `1` before the `next = 2`
write. This is consistent with gate 1 firing after the player is attached (stage 2 progressed,
`0x18B` set), but `0x18B` is not a gate for state 2 (it gates only the state 3 chain). So the
causal path is: stage 2 progresses -> player attach/seat state matures ->
`sub_140DBEA00(player, 0xED)` returns true -> `next = 2` -> Drive. `0x18B` is a side signal,
not the trigger.

## Open: `rideRuntime + 0x192` (b192) setter

b192 is cleared by `OnExit` (`0x140F999B1`) but no setter was found in:
- `OnEnter` (`0x140F98CE0`)
- `OnExit` (`0x140F99990`)
- `RideRuntime_UpdateBaggageEventAndSeatFlags` (`0x141011BF0`) — writes `rideRuntime + 0x470`,
  `[qword_14623E948 + 0x24290] + 0xBE / +0xBF`, and baggage events; no `+0x192` write.
- `RideOnState_Update` (`0x140F99C40`) — only reads `+0x192`.

Runtime showed b192 = 0 throughout normal boarding. The setter is therefore some other
function not yet inspected. Finding it without a global search requires following the
rideRuntime call graph by hand (e.g. `RideRuntime_OnExitUpdateBaggageMode` `0x1410115E0`,
`sub_14100FD30`, the seat-animation helper `0x141012280`, etc.). Not resolved here.

## Open: `sub_140DBEA00(playerEntity, 0xED)` query semantics

The query is polymorphic: `playerEntity` vtable `+0xA0` (called twice, once with no id and
once with id `0xED`) and `playerEntity + 0x8A8` sub-component vtable `+0xE0`. To know what
player state makes it return true, the concrete player entity vtable and the
`+0x8A8` component vtable must be resolved. `playerEntity = *(qword *)(state + 0x98)`, the
same pointer used as `rcx` at the attach callsite `0x140F9AD80`. Not resolved here.

## Open: `state + 0xA8 -> 0x3970` byte (== 9 / == 3)

OnEnter arms gate 2's b190 only when `*(byte *)(*(state + 0xA8) + 0x3970) == 9`, and sets
`state + 0x19C = 1` for `== 3` or `== 9`. `state + 0xA8` is the vehicle/seat context object
also used by OnExit and by the seat-resolution code. The byte at `+0x3970` is most likely a
vehicle/seat boarding-context enum; `9` is the only observed value that arms b190. Its
concrete meaning and the set of vehicles/contexts that yield `9` are not resolved here.

## Feasibility analysis: data/state-driven completion paths

Per the branch goal (prove scope and feasibility of data/state-driven RideOn completion
paths, without register-context or call-context detour schemes), the three gates are:

1. Gate 1 (`sub_140DBEA00(player, 0xED)`): data/state driven via the player entity and its
   `+0x8A8` component. This is the gate the normal run actually used. It matures after the
   player is attached. Scope: this is the game-native completion signal; it cannot fire
   before the player entity state is ready, so it does not by itself provide an "earlier
   than native" completion.

2. Gate 2 (`b190 && b192`): pure data — two bytes in `rideRuntime`. If both are nonzero, the
   update requests Drive immediately, bypassing both the player query and the 5.0s timer.
   Scope: b190 is armed only for boarding context `9`; b192's setter is unknown and b192
   stayed `0` in normal boarding. So in normal boarding this gate is inert. Whether any
   boarding context sets both bytes is an open question. This gate is the most directly
   "data-only" completion path, but its feasibility as an early-completion trigger depends
   on the unresolved b192 setter and the context-9 condition; it is not yet a confirmed
   usable path.

3. Gate 3 (`elapsed >= 5.0`): data — a float constant at `dword_143461CCC`. Zeroing it
   crashed the game before reaching the save flow (prior experiment, removed), so the
   constant is not safely patchable in isolation; the failure mode is not yet understood.

None of the three gates depends on register context or on a call-site detour. They are all
read from memory fields or constants. The blocker chain (state 3 path) is similarly
data/state driven.

## Attempted direct verification of gate 1 — INFEASIBLE (verified)

A read-only entry detour on `EntityComponent_QueryBoolById` (`0x140DBEA00`) was implemented
and built into the trace ASI (patchLen 15, trampoline over the clean prologue
`mov [rsp+arg_10],rbp; push rsi; sub rsp,20h; mov rax,[rcx]; mov ebp,edx`, jump back to
`0x140DBEA0F`; no RIP-relative instruction in the patched range; no external xref into the
first 15 bytes — only internal fall-through). The hook only called the original and logged
when `id == 0xED && entity == active RideOn player && current == 1`.

Runtime result: the game ran ~22s after ASI load (through boot, music load, jacket apply)
then exited ~10s into save load (after the recover-confirm keys), BEFORE any RideOn `Init`
line. No `QueryGate1` log was produced. Reverting to the known-good 3-hook build
(Init/Update/Abort only) and re-running the same script completed launch, board, dismount,
quit without crash. So the crash is caused by the `EntityComponent_QueryBoolById` detour.

Root cause assessment: `EntityComponent_QueryBoolById` has 90+ code xrefs (verified via
`xrefs_to 0x140DBEA00`), i.e. it is a very hot generic entity query called heavily during
save/entity-streaming load. Even a read-only entry detour on such a hot function disrupts
the save-load phase. The trampoline itself is structurally correct; the failure is the
function's traffic profile, not the patch layout. Deferring install until RideOn `Init` was
rejected because patching a concurrently-executing hot function from the game thread is a
race risk.

Conclusion: direct hook verification of gate 1 via this function is not feasible with the
simple trampoline framework. The gate-1 deduction above (eliminative, from the verified
three-gate CFG and the existing 3-hook runtime snapshots) remains the verification of
record. Do not re-add a `0x140DBEA00` entry detour to the trace ASI.
