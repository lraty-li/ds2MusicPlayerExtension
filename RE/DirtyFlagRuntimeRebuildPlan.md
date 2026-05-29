# Dirty Flag Runtime Rebuild Plan

## Goal

When browser metadata changes, the plugin should not rebuild the music player
runtime directly. It should only update the injected track/album text and mark
the game-owned music runtime data as dirty. The game should then rebuild or
refresh its own runtime entry/cache through its normal update path.

Expected behavior:

- Browser sends new title/artist.
- ASI writes the new text into the injected track and album resources.
- ASI marks the relevant music runtime state as dirty.
- A game-owned update/menu/music-runtime path observes the dirty state.
- The game rebuilds or refreshes the runtime entry data.
- Current UI/title display updates without forcing pause/play or replaying the
  track.

## Ownership Model

The plugin owns only:

- Browser metadata polling.
- Injected track resource title text.
- Injected album artist text.
- Dirty flag writes.
- Logging and validation.

The game owns:

- Runtime entry list rebuild.
- Track id to entry mapping.
- Current player object lifecycle.
- UI/list/telop binding refresh.
- Any locking, thread affinity, or update ordering around music runtime data.

The plugin should not create a new player object, restart the current entry, or
manually drive the music state machine for this scheme.

## Relevant Runtime State

Known runtime fields from current investigation:

```text
MusicRuntime + 0x1910 : playState
MusicRuntime + 0x1918 : current player/runtime object
MusicRuntime + 0x1924 : current track id
MusicRuntime + 0x1930 : current queue index
MusicRuntime + 0x1938 : runtime entry list/vector area
MusicRuntime + 0x1970 : start of fixed track-id mapping table area
```

Known global resources:

```text
DS2 + 0x6232DE8 : global MusicRuntime pointer
DS2 + 0x6232DC8 : global DSMusicPlayerSystemResource pointer
```

Injected resource text fields:

```text
TrackResource + 0x38 : title localized text
AlbumResource + 0x30 : artist localized text
AlbumResource + 0x40 : telop artist localized text
```

The exact dirty flag or invalidation field is not yet confirmed. It should be
derived from the game code path that refreshes `MusicRuntime + 0x1938` and the
mapping table after resource changes.

## Dirty Flag Contract

The dirty marker should mean:

```text
Runtime entry cache no longer matches DSMusicPlayerSystemResource.
Refresh/rebuild entries on the next safe game-owned update point.
```

The marker should be set only after text writes are complete.

Suggested state shape in ASI:

```cpp
struct MetadataDirtyState
{
    uint32_t generation;
    uint32_t lastAppliedGeneration;
};
```

Plugin-side flow:

```text
metadata changed
  -> update injected track title
  -> update injected album artist/telop artist
  -> increment plugin metadata generation
  -> set game dirty marker
```

Game-owned flow:

```text
game update/menu/music runtime sees dirty marker
  -> rebuild/refresh runtime entries from DSMusicPlayerSystemResource
  -> clear dirty marker
  -> UI/list/telop reads refreshed runtime entry data
```

## Threading Rule

The metadata thread may update the injected resource text and set a simple dirty
marker only if the target field is safe for cross-thread writes.

If the dirty marker belongs to music runtime state that is not safe to write
from the metadata thread, the metadata thread should only set an ASI-local
atomic pending flag:

```text
metadata thread: pendingDirty = true
game hook thread: if pendingDirty, set game dirty marker
```

The dirty marker should then be written from a natural game thread hook such as:

- music runtime update/tick
- music menu controller update
- runtime entry/list access path
- state transition hook when already inside `MusicRuntime`

## Candidate Discovery Targets

The dirty marker should be found by inspecting code that naturally refreshes or
rebuilds runtime entries. Candidate code areas:

```text
MusicRuntime_RebuildEntriesFromMusicResource
  reads DSMusicPlayerSystemResource
  rewrites runtime entry list/mapping

menu open / list rebuild controller path
  triggers visible track list refresh

first play / apply entry path
  consumes current runtime entry and binds title data

music runtime update/tick
  safe recurring game-owned point for deferred refresh
```

The search should identify:

- field written before rebuild to request refresh
- field cleared after rebuild completes
- generation/version counter if present
- list invalidation flag if present
- menu/list datasource dirty bit if separate from runtime dirty bit

## Implementation Plan

1. Add an ASI-local metadata generation counter.
2. On title/artist change, update injected resource text.
3. Set `metadataDirtyPending = true`.
4. In a safe game-thread hook, consume `metadataDirtyPending`.
5. Write the confirmed game dirty marker.
6. Log the marker write with generation id.
7. Log when the game-owned path observes or clears the marker.
8. Confirm UI/title refresh occurs without restarting current playback.

Expected log shape:

```text
dynamic title sync set title="..."
dynamic title sync set artist="..."
music runtime dirty requested generation=N
music runtime dirty marker set generation=N
music runtime dirty marker consumed generation=N
```

## Validation Criteria

The scheme is considered valid only if:

- metadata changes while the external track is already playing
- no user menu open/close action is required
- no pause/play pulse is required
- no current player object is recreated
- `currentTrackId` remains `0xAD900001`
- `playState` remains stable or follows normal game-owned transitions
- UI/title display updates after the game consumes the dirty marker

## Open Items

The exact game dirty marker is still the missing piece.

Required next investigation:

- locate the field or flag that causes runtime entries to be rebuilt from
  `DSMusicPlayerSystemResource`
- determine which thread/path may safely set that field
- confirm whether runtime entry cache dirty and UI datasource dirty are separate
  markers
