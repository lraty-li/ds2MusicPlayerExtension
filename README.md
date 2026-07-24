
# DS2MusicPlayer

DS2MusicPlayer adds a special music-player track to Death Stranding 2. When the
track is played in game, audio is streamed from a Chrome/Edge tab through a
runtime Wwise SourcePlugin.

## Preview

[bilibili](https://www.bilibili.com/video/BV1t1RrBNEjm/)

## User installation

Download the latest `DS2MusicPlayer-*.zip` from GitHub Releases and extract it.

Copy the contents of the extracted `DS2MusicPlayer` folder into the game root:

```text
<GameRoot>\version.dll
<GameRoot>\scripts\Ds2MusicPlayerExtend.asi
<GameRoot>\scripts\ds2_dll_music_resource.dll
<GameRoot>\scripts\DS2SpotifyConnectBridge.dll
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

The extension runs in controlled mode. There is no popup UI; clicking the icon
only starts or stops tab-audio streaming. Browser playback pause/resume is
driven by the in-game player state through the local WebSocket connection.

For NetEase Cloud Music, start playback manually in the page once before using
the game-side controls. Chrome may reject a cold `play()` request until the page
has received user interaction; after that first manual start, DS2 can take over
pause/resume synchronization.

## Spotify Connect

The packaged bridge DLL is loaded automatically by the ASI into the game process.
It starts and stops with the game; no separate bridge process is created.
In Spotify's device picker, select `Death Stranding 2`; Spotify audio, title,
artist, and album art then stream directly into the special in-game track.

Use either the browser extension or Spotify Connect for a playback session, not
both at once. The game pause/resume state controls the selected Spotify Connect
device; Spotify-side controls do not change the game player's state.

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

## Nexus Mods Releases

The same release workflow can also publish the package to Nexus Mods. Configure
these repository settings first:

```text
Secret:   NEXUSMODS_API_KEY
Variable: NEXUSMODS_FILE_ID
```

`NEXUSMODS_FILE_ID` is the existing Nexus Mods v3 mod-file ID used by
`POST /mod-files/{id}/versions`; it is not the game mod page number. Create the
Nexus Mods mod page and first file manually, then use that file ID for automated
version uploads.

After those values are set, pushing a `v*` tag publishes both the GitHub Release
and a Nexus Mods file version. For a manual workflow run, set
`publish_nexusmods` to `true`.

## Credit

Thanks to plyrthn:
https://github.com/plyrthn/ds2_sd_card_music_player.git
