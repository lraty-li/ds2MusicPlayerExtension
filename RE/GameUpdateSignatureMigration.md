# Game update failure and signature migration notes

Date: 2026-05-08

Scope: static code review only. The game was not launched for this pass, so this
document identifies code-level update risks rather than confirming the current
runtime failure log.

## Finding

The current ASI is not fully signature based. It has three categories of game
integration:

1. Pattern-scanned function/global discovery.
2. Hardcoded function RVA.
3. Hardcoded object layout offsets and vtable indices after an object is found.

This means a game update can still break the plugin even when some entry points
use signatures.

## Critical entry point migration

`ds2_music_player_asi/WwisePluginRegistration.cpp` used to resolve
`RegisterPluginDLL` by fixed RVA:

```cpp
constexpr uintptr_t kRegisterPluginDllRva = 0x28A1330;
const uintptr_t registerAddress =
    reinterpret_cast<uintptr_t>(gameModule) + kRegisterPluginDllRva;
```

This was the most likely "whole plugin no longer works after update" failure.
If the game update moves `RegisterPluginDLL`, the ASI may call the wrong code or
fail the module-range guard. In that case the runtime Wwise source plugin is not
registered, and loading the generated source-plugin bank can fail with plugin
registration related errors.

Current implementation resolves the function through `GameSymbols`:

1. Prefer the exported decorated Wwise symbol:
   `?RegisterPluginDLL@SoundEngine@AK@@YA?AW4AKRESULT@@PEB_W0@Z`
2. Fall back to a byte signature matching the current function body.
3. Refuse to call if neither resolver succeeds.

Current logging around this path:

- `RegisterPluginDLL resolved by export rva=...`
- `RegisterPluginDLL resolved by signature rva=...`
- `stream plugin register skipped: RegisterPluginDLL unresolved`
- `RegisterPluginDLL result=<value>`
- `stream source plugin registration failed`

## Existing signature-based paths

`MusicPlayerInjection.cpp` pattern-scans a `StreamingManager` global store:

```cpp
48 89 05 ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 D2
41 B8 F8 0A 00 00 48 8B C8 48 8B D8 E8
```

After the scan, it still assumes:

- RIP operand offset: `3`
- `StreamingManager + 0x578` is the streaming system
- streaming system vtable slot `3` is `AddListener`

So the global location is signature based, but the object layout and vtable
contract are not.

`PlayStateMonitor.cpp` pattern-scans the play-state setter:

```cpp
40 57 48 83 EC 20 0F B6 81 10 19 00 00 48 8B F9
3A C2 0F 84 ?? ?? ?? ?? 3C 05 75 07 C6 81 B6 28 00 00 00
```

After the scan, it still assumes:

- state offset: `0x1910`
- current runtime offset: `0x1918`
- current track id offset: `0x1924`
- patch length: `13`

If the function changes, the signature can fail. If the function is still found
but object offsets change, pause/resume synchronization can silently misread
state or track id.

## Object layout offsets still hardcoded

The injected external track and metadata sync rely on many fixed offsets. Main
examples:

- `DSMusicPlayerSystemResource + 0x30`: all tracks array
- track `+0x30`: album resource
- track `+0x38`: title text
- track `+0x40` / `+0x48`: sound resource pointers
- track `+0x20`: track id
- album `+0x30`: artist text
- album `+0x40`: telop artist text
- graph chain offsets: `0x288`, `0x0B8`, `0x40`, `0x20`
- mutable localized text string pointer and length: `+0x20`, `+0x28`

These are not image RVAs, so signature scanning alone does not solve them. They
need either type metadata discovery, runtime validation, or signatures that lead
to field access instructions.

## What cannot be migrated by plain byte signatures

There is no current feature that is conclusively impossible to make
update-tolerant. However, several parts cannot be migrated by simply replacing a
constant with a byte pattern that returns one address.

The following items need stronger resolvers than plain function-entry signature
scans:

- Object field offsets such as track title, album artist, sound resource, and
  `MusicRuntime` state fields. These are data-layout facts, not executable entry
  points. A robust resolver has to derive them from field-access instructions,
  type metadata, or validated runtime objects.
- Vtable slots such as streaming-system `AddListener`. A byte pattern can find
  code that performs the call, but the slot number itself should be validated
  against call context or resolved through a known caller. It is not a stable
  standalone byte sequence.
- Clone sizes for track, album, graph sound resource, graph program resource,
  node container resource, and Wwise-id objects. Signatures can locate
  constructors, allocators, destructors, or copy paths, but object size needs to
  be derived from those code paths or metadata. A raw size constant has no
  unique signature by itself.
- Localized-text string layout. The string pointer and length fields can likely
  be resolved from the text constructors/readers, but the mutable write path
  still needs runtime validation because a wrong text-object layout corrupts
  game memory.
- Resource array invariants. The `RawArray` shape and `DSMusicPlayerSystemResource`
  all-tracks member can be located from known users, but correctness depends on
  validating count/capacity/entries and element RTTI at runtime.

The practical conclusion is that `RegisterPluginDLL` can be migrated directly to
a function resolver, while the track injection and metadata sync paths need
symbol resolvers that recover both addresses and layout facts, then validate the
result before writing memory.

## Why this update can break the plugin

Before the resolver migration, the most direct reason was the hardcoded
`RegisterPluginDLL` RVA. The user-facing symptom "plugin cannot be used" is
consistent with the Wwise source plugin never being registered.

Even after fixing that RVA, the following paths can break independently:

- music resource listener fails if the `StreamingManager` signature changes
- listener installs but reads the wrong streaming system if `+0x578` changes
- external track injection fails if `DSMusicPlayerSystemResource` layout changes
- pause synchronization fails if `MusicRuntime` state/track offsets change
- dynamic title/artist sync fails if track/album/text layouts change

## Migration direction

1. Continue expanding the resolver layer after the completed
   `RegisterPluginDLL` migration.
   - Current resolver prefers the exported Wwise symbol and uses a function-body
     signature as fallback.
   - Add each future address/offset to the same symbol-resolution path instead
     of keeping scattered constants in feature modules.

2. Centralize all game-derived addresses and offsets.
   - Create a resolver module that returns a single `GameSymbols` structure.
   - Do not leave scattered constants in feature modules.

3. Treat field offsets as resolved symbols.
   - Keep current constants only as fallback or validation expectations.
   - Where possible, resolve offsets from field metadata or from field-access
     instruction patterns.

4. Add strict runtime validation.
   - Validate candidate function addresses are inside `.text`.
   - Validate object pointers are inside module/heap-readable ranges before use.
   - Validate RTTI names and array invariants before writing injected resources.

5. Fail feature-by-feature instead of failing opaquely.
   - Wwise plugin registration
   - generated source bank load
   - music resource listener
   - external track injection
   - pause state monitor
   - dynamic title/artist sync

6. Log every resolved symbol with source.
   - Example: `RegisterPluginDLL resolved by signature rva=...`
   - Example: `MusicRuntime offsets state=... trackId=... source=pattern`

## Immediate priority

`RegisterPluginDLL` has been migrated first because it was the only critical
function entry still resolved by fixed image RVA and could prevent the whole
audio source plugin from registering after any game update.
