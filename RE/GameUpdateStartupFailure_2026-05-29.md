# Game Update Startup Failure - 2026-05-29

## Symptom

After the game update, the game process does not remain running when the current
ASI is installed.

## Runtime Evidence

Latest `log.txt` shows the ASI enters normally and resolves several old
integration points:

```text
StreamingManager global slot=00007FF7BF7C67E0
music listener registered
play state monitor installed at rva=0xC162D0
play entry monitor installed at rva=0xC12580
```

The Wwise plugin registration path no longer fails because the export is
missing. The updated game still exports `RegisterPluginDLL`, but calling it now
throws inside the game function:

```text
RegisterPluginDLL resolved by export rva=0x28AABF0
RegisterPluginDLL exception code=0xC0000005 address=0000000000000000
stream source plugin registration failed
```

Despite this failure, the old ASI continued to mutate the music resource:

```text
OnFinishLoadGroup: DSMusicPlayerSystemResource found
injected special music track id=0xad900001 event=0xad100000
custom bank/source-plugin event is still required before audio can play
dynamic title sync started
```

This is unsafe after an update because both the source-plugin bank is absent and
several music resource layout offsets are still hardcoded.

## Immediate Mitigation

The ASI first fails closed:

1. `PlayEntryMonitor` is skipped because it still uses fixed RVA `0xC12580`.
2. Special track injection only runs after `RegisterPluginDLL` and
   `LoadBankMemoryCopy` both succeed.
3. If source audio is not ready when `DSMusicPlayerSystemResource` loads, the
   listener logs `music injection skipped: source audio bank not ready` and
   leaves the game resource untouched.

This should allow the updated game to start even while the external music
feature is unavailable.

## Functional Fallback Attempt

Static inspection of the updated export shows the wrapper still performs:

```text
LoadLibraryW(resolved plugin path)
GetProcAddress(module, "g_pAKPluginList")
InternalRegisterPluginList(*g_pAKPluginList)
```

The internal plugin-list registrar is reached by a relative call after:

```text
48 8B 08    mov rcx, [rax]
E8 xx xx xx xx
```

The ASI now resolves that relative call from the live `RegisterPluginDLL`
function body. If the wrapper throws, it calls the internal registrar directly
with the already loaded plugin DLL's `*g_pAKPluginList`.

Expected new log lines:

```text
RegisterPluginDLL exception code=0xC0000005 address=0000000000000000
RegisterPluginList resolved from wrapper rva=...
RegisterPluginList fallback result=1
```

If the fallback returns `1`, the normal generated source-plugin bank load path
continues and special track injection is allowed.

## Remaining Reverse Engineering Work

The new `RegisterPluginDLL` failure must be investigated in the updated binary.
The current call target is valid. If the direct registrar fallback also fails,
the likely break is one of:

- the exported function's expected arguments changed,
- the plugin list ABI/layout changed,
- the function now depends on additional Wwise initialization state,
- the game now rejects or dereferences a field that the current runtime DLL
  registration table leaves null.

The hardcoded `PlayEntryMonitor` RVA should not be re-enabled until it is
replaced with a signature or removed from normal builds.
