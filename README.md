
# DS2MusicPlayer

DS2MusicPlayer adds a special music-player track to Death Stranding 2. When the
track is played in game, audio is streamed from a Chrome/Edge tab through a
runtime Wwise SourcePlugin.

## User installation

Download the latest `DS2MusicPlayer-*.zip` from GitHub Releases and extract it.

Copy the contents of the extracted `DS2MusicPlayer` folder into the game root:

```text
<GameRoot>\version.dll
<GameRoot>\scripts\Ds2MusicPlayerExtend.asi
<GameRoot>\scripts\ds2_dll_music_resource.dll
```

The game root is the folder that contains the game executable. `version.dll` is
the ASI loader, and the ASI plus audio DLL must stay in the `scripts` folder.

Load the browser extension:

1. Open `chrome://extensions` or `edge://extensions`.
2. Enable Developer mode.
3. Click `Load unpacked`.
4. Select the extracted `browser-extension` folder.

Use it:

1. Open a browser tab that is playing audio.
2. Click the DS2MusicPlayer extension icon.
3. Start the game and play the injected special music-player track.

The extension badge shows `WAIT` while it is waiting for the game-side plugin,
and `PCM` when audio is streaming.

## Building a release package

Run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\package-release.ps1
```

The output zip is written to:

```text
dist\
```

To choose a package name suffix:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\package-release.ps1 -PackageVersion v0.1.0
```

This produces `dist\DS2MusicPlayer-v0.1.0.zip`.

The script builds the ASI and runtime audio DLL, downloads `version.dll` from
Ultimate ASI Loader, copies the browser extension files, and writes a bilingual
`README.txt` into the package.

## GitHub Releases

Pushing a tag named `v*` runs `.github/workflows/release.yml` and attaches the
package zip to the GitHub Release:

```powershell
git tag v0.1.0
git push origin v0.1.0
```

## Credit

Thanks to plyrthn:
https://github.com/plyrthn/ds2_sd_card_music_player.git
