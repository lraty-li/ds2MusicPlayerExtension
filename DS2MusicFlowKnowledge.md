# DS2 Music Flow Knowledge

This file records only facts observed from runtime logs during manual interaction.

## Idle cursor movement

Observed user action:

- Move the cursor between tracks while nothing is playing.

Observed hooks:

- `sub_140C155F0`

Observed repeated state:

- `state1910=0`
- `trackId=0`
- `queueIndex=17`
- `currentRuntime=0x0`
- `state2826=0`
- `trialValue=0`
- `trialRuntime=0x0`
- `currentTrialRuntimeObject=0x0`

## Direct play from idle

Observed user action:

- Click the play button while no track is currently playing.

Observed hooks and values:

- `sub_140C12580`
  - `trackId=28`
  - `soundResource=0x23443550D50`
  - `trialSoundResource=0x23443553180`
- First `sub_140AC5210`
  - `callerTag=play.current.create`
  - `soundResource=0x23443550D50`
  - `runtimeObject=0x2FC32B18F80`
  - `flags180=50`
- `sub_140C155F0`
  - `currentTrialRuntimeObject=0x0`
- `sub_140C155F0 managerState.after`
  - `state1910=5`
  - `trackId=28`
  - `queueIndex=17`
  - `currentRuntime=0x2FC32B18F80`
  - `state2826=0`
  - `trialRuntime=0x0`

Observed additional creates during the same play action:

- `sub_140AC5210`
  - `callerTag=play.queue.primary.create`
  - `flags180=51`
- `sub_140AC5210`
  - `callerTag=play.queue.trial.create`
  - `flags180=51`

## Play again while already playing

Observed user action:

- Click the play button again while a track is already playing.

Observed hooks and values:

- `sub_140C155F0`
- No new `sub_140C12580` was present in that sample.
- No new `play.current.create` was present in that sample.
- `sub_140C155F0 managerState.after`
  - `state1910=1`
  - `trackId=28`
  - `queueIndex=17`
  - `currentRuntime=0x34AA6F03480`
  - `state2826=0`
  - `trialRuntime=0x0`

## Next track

Observed user action:

- Click the next-track button while a track is already playing.

Observed hooks and values:

- `sub_140C14CB0`
  - `currentRuntimeObject=0x34AA6F03480`
- `sub_140AC5320`
  - `callerTag=play.current.destroy`
  - `runtimeObject=0x34AA6F03480`
  - `flags180=32`
- `sub_140C14CB0 managerState.after`
  - `state1910=2`
  - `trackId=28`
  - `queueIndex=6`
  - `currentRuntime=0x0`
  - `state2826=0`
  - `trialRuntime=0x0`
- Next `sub_140AC5210`
  - `callerTag=advance.current.create`
  - `soundResource=0x1BFC38BF740`
  - `runtimeObject=0x34AA6F96200`
  - `flags180=50`

## Preview / sample playback

Observed user action:

- Click the preview/sample play button.

Observed hooks and values on preview create:

- `sub_140C15560`
  - `arg0` matched `manager`
  - example observed:
    - `trackId=5`
    - `trialSoundResource=0x23443896E30`
- `sub_140C15560 managerState.before`
  - example observed:
    - `state1910=2`
    - `trackId=28`
    - `queueIndex=17`
    - `currentRuntime=0x2FC32B18F80`
- `sub_140C15560 managerState.before`
  - another observed sample:
    - `state1910=0`
    - `trackId=0`
    - `queueIndex=17`
    - `currentRuntime=0x0`
    - `state2826=1`
    - `trialValue=0`
    - `trialRuntime=0x0`
- `sub_140AC5210`
  - `callerTag=preview.create`
  - `soundResource=0x23443896E30`
  - `runtimeObject=0x2FC32B1D580`
  - `flags180=50`
- `sub_140AC5210`
  - another observed sample:
    - `callerTag=preview.create`
    - `soundResource=0x205C3896E30`
    - `soundResource.qwords+0x20=0x205C2880678 0x3F800000 0x3F80000000000000 0x0`
    - `runtimeObject=0x5F78739FC00`
    - `control60=0`
    - `sourceObject170=0x0`
    - `linkedObject178=0x5F7A9FB3F00`
    - `state134=1`
    - `flags180=50`
    - `state1E0=0`
    - `state245=0`
    - `ref2BA=0`
- `sub_140C15560 managerState.after`
  - `state1910=2`
  - `trackId=28`
  - `queueIndex=17`
  - `currentRuntime=0x2FC32B18F80`
  - `state2826=2`
  - `trialValue=0`
  - `trialRuntime=0x2FC32B1D580`
- `sub_140C15560 managerState.after`
  - another observed sample:
    - `state1910=0`
    - `trackId=0`
    - `queueIndex=17`
    - `currentRuntime=0x0`
    - `state2826=2`
    - `trialValue=0`
    - `trialRuntime=0x5F78739FC00`
- `sub_140C15560 trialRuntimeObject`
  - `object=0x2FC32B1D580`
  - `flags180=54`
- `sub_140C15560 trialRuntimeObject`
  - another observed sample:
    - `object=0x5F78739FC00`
    - `control60=0`
    - `sourceObject170=0x0`
    - `linkedObject178=0x5F7A9FB3F00`
    - `state134=1`
    - `flags180=54`
    - `state1E0=0`
    - `state245=0`
    - `ref2BA=0`

Observed hooks and values when preview is re-triggered while a preview runtime already exists:

- `sub_140C15560 managerState.before`
  - `trialRuntime=0x2FC32B19D80`
  - `state2826=2`
- `sub_140C155F0`
  - `currentTrialRuntimeObject=0x2FC32B19D80`
- `sub_140AC5320`
  - `callerTag=preview.destroy`
  - `runtimeObject=0x2FC32B19D80`
  - `flags180=32`
- `sub_140C155F0 managerState.after`
  - `state2826=0`
  - `trialRuntime=0x0`
- New `sub_140AC5210`
  - `callerTag=preview.create`
  - new `runtimeObject` observed afterward
- New `sub_140C15560 managerState.after`
  - `state2826=2`
  - new `trialRuntime` observed afterward

Observed standalone later preview destroy:

- `sub_140AC5320`
  - `callerTag=preview.destroy`
- `sub_140C155F0 managerState.after`
  - `state2826=0`
  - `trialRuntime=0x0`
  - `currentRuntime` remained the active playback object in that sample
