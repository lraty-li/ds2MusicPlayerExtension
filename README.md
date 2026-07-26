
# DS2MusicPlayer

DS2MusicPlayer adds a special music-player track to Death Stranding 2. Audio can
come from either a Chrome/Edge tab or a Spotify Connect device backed by the
official Spotify Web Playback SDK.

## Preview

[bilibili](https://www.bilibili.com/video/BV1t1RrBNEjm/)

## User installation

Download the latest `DS2MusicPlayer-*.zip` from GitHub Releases and extract it.

Copy the contents of the extracted `DS2MusicPlayer` folder into the game root:

```text
<GameRoot>\version.dll
<GameRoot>\scripts\Ds2MusicPlayerExtend.asi
<GameRoot>\scripts\ds2_dll_music_resource.dll
<GameRoot>\scripts\DS2SpotifyHelper\DS2SpotifyWebView2Helper.exe
<GameRoot>\scripts\DS2SpotifyHelper\config.json
<GameRoot>\scripts\DS2SpotifyHelper\web\
```

The game root is the folder that contains the game executable. `version.dll` is
the ASI loader, and the ASI plus audio DLL must stay in the `scripts` folder.

### Optional browser tab playback

To use browser-tab playback, load the optional browser extension:

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

The ASI starts the packaged WebView2 helper as an external process. During
playback its tool window remains visible to WebView2 but is positioned outside
the virtual desktop and excluded from the taskbar and Alt-Tab. The helper is
placed in a kill-on-close job, so it exits with the game even after a crash.
Its silent Web Audio sink belongs only to this helper and does not mute the
user's Edge browser sessions.

On the first run without a saved PKCE authorization, the helper window appears
for authorization. Later runs stay hidden. In Spotify's device picker, select
`Death Stranding 2`; Spotify audio then streams into the special in-game track.

The browser extension and Spotify helper may remain connected together. The
most recent explicit playback action owns the stream: starting Spotify preempts
and pauses the captured tab; resuming the captured tab reclaims it.
Reconnection alone does not claim ownership.

The game pause/resume state controls the active source. Spotify-side controls
do not change the game player's state.

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

The script builds the ASI, runtime audio DLL, and WebView2 Spotify helper,
downloads `version.dll` from Ultimate ASI Loader, copies the browser extension
files, and writes a bilingual `README.txt` into the package.

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
