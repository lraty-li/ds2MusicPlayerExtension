
# DS2 Vehicle Boarding Fast-Forward

A Death Stranding 2 ASI plugin that speeds up the vehicle boarding animation.

When Sam enters a vehicle seat, the boarding animation plays in fast-forward
instead of real time, reducing waiting and getting you into control faster.

## User installation

Download the latest `DS2VehicleBoardingFastForward-*.zip` from GitHub Releases
and extract it. Copy `Ds2VehicleBoardingFastForward.asi` into the game scripts
folder:

```text
<GameRoot>\scripts\Ds2VehicleBoardingFastForward.asi
```

The game root is the folder that contains the game executable. You also need
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) —
download `version-x64.zip`, extract `version.dll`, and place it in the game
root:

```text
<GameRoot>\version.dll
```

## Building

### From Visual Studio

Open `ds2_vehicle_boarding_trace\ds2_vehicle_boarding_trace.sln` and build
Release x64.

### From command line

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\ds2_vehicle_boarding_trace\build.ps1
```

The output `.asi` is written directly to the game scripts folder.

### Building a release package

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\package-release.ps1
```

Output goes to `dist\DS2VehicleBoardingFastForward-<version>.zip`.

## GitHub Releases

Pushing a tag named `v*` runs `.github/workflows/release.yml` and attaches the
package zip to the GitHub Release:

```powershell
git tag v0.1.0
git push origin v0.1.0
```

## Nexus Mods Releases

The same release workflow can publish to Nexus Mods. Configure these repository
settings first:

```text
Secret:   NEXUSMODS_API_KEY
Variable: NEXUSMODS_FILE_ID
```

After those values are set, pushing a `v*` tag publishes both the GitHub Release
and a Nexus Mods file version. For a manual workflow run, set
`publish_nexusmods` to `true`.
