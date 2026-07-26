
# DS2MusicPlayer

DS2MusicPlayer adds a special music-player track to Death Stranding 2. Audio can
come from either a Chrome/Edge tab or a Spotify Connect device backed by the
official Spotify Web Playback SDK.

## Preview

[bilibili](https://www.bilibili.com/video/BV1t1RrBNEjm/)

## User installation

Choose one package from GitHub Releases:

- `DS2MusicPlayer-v*.zip`: base edition for browser-tab playback. It never
  starts the Spotify WebView2 helper.
- `DS2MusicPlayer-Spotify-v*.zip`: Spotify edition with both browser-tab
  playback and Spotify Connect.

The editions are separate because Spotify Connect requires a hidden Edge
WebView2 process group. It added about 400 MiB of private committed memory on
the test system; the exact cost varies by WebView2 version and machine state.
Users who do not need Spotify should install the base edition to avoid this
resident memory overhead.

Extract the selected package.

Copy the contents of the extracted `DS2MusicPlayer` folder into the game root:

```text
<GameRoot>\version.dll
<GameRoot>\scripts\Ds2MusicPlayerExtend.asi
<GameRoot>\scripts\ds2_dll_music_resource.dll
<GameRoot>\scripts\ds2_jacket_bc7e.dll
```

The game root is the folder that contains the game executable. `version.dll` is
the ASI loader, and the ASI plus audio DLL must stay in the `scripts` folder.
The Spotify edition additionally installs `scripts\DS2SpotifyHelper`. The base
ASI never starts that helper, even if the folder remains from an older install.

### Browser tab playback

Both editions include the browser extension. Load it to use browser-tab
playback; Spotify-only users may leave it unloaded:

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

Spotify Connect requires the `DS2MusicPlayer-Spotify-v*.zip` edition.

Before the first launch:

1. A Spotify Premium account is required.
2. Sign in to the
   [Spotify Developer Dashboard](https://developer.spotify.com/dashboard) and
   create an app for personal use. Select Web Playback SDK when asked which
   API/SDK the app will use.
3. Open the app settings and add this exact Redirect URI:

   ```text
   https://appassets.example/index.html
   ```

4. Copy the app's Client ID into
   `scripts\DS2SpotifyHelper\config.json`:

   ```json
   {
     "spotifyClientId": "your 32-character Client ID",
     "proxyServer": "",
     "diagnostics": false
   }
   ```

Only the public Client ID is needed; never put the Client Secret in this file.
Spotify Development Mode apps have an authorized-user limit, so each user
should normally create their own app instead of sharing the package author's
Client ID.

The ASI starts the packaged WebView2 helper as an external process. During
playback its tool window remains visible to WebView2 but is positioned outside
the virtual desktop and excluded from the taskbar and Alt-Tab. The helper is
placed in a kill-on-close job, so it exits with the game even after a crash.
Its silent Web Audio sink belongs only to this helper and does not mute the
user's Edge browser sessions.

On the first run without a saved PKCE authorization, the Spotify authorization
page opens directly; the helper dashboard is not loaded. Later runs stay
hidden. In Spotify's device picker, select `Death Stranding 2`; Spotify audio
then streams into the special in-game track.

Production mode also skips the diagnostic probes, metrics UI, and telemetry
logging. Set `"diagnostics": true` in `config.json` only while troubleshooting;
that explicitly loads and displays the full helper dashboard.

The browser extension and Spotify helper may remain connected together. The
most recent explicit playback action owns the stream: starting Spotify preempts
and pauses the captured tab; resuming the captured tab reclaims it.
Reconnection alone does not claim ownership.

The game pause/resume state controls the active source. Spotify-side controls
do not change the game player's state.

## Building a release package

Run from the repository root:

```powershell
.\package-release.ps1 -Edition Base -PackageVersion v2.0
.\package-release.ps1 -Edition Spotify -PackageVersion v2.0
```

This writes two packages to `dist\`:

```text
DS2MusicPlayer-v2.0.zip
DS2MusicPlayer-Spotify-v2.0.zip
```

The base edition is the default when `-Edition` is omitted. Spotify diagnostic
packages are built explicitly:

```powershell
.\package-release.ps1 -Edition Spotify -PackageVersion v2.0-diagnose -Diagnostic
```

Both editions contain the ASI, runtime audio DLL, browser extension, BC7
encoder, and Ultimate ASI Loader. Only the Spotify edition builds and packages
the WebView2 helper.

## GitHub Releases

Pushing a normal `v*` tag runs `.github/workflows/release.yml` and attaches both
the base and Spotify packages to one GitHub Release:

```powershell
git tag v2.0
git push origin v2.0
```

## Nexus Mods Releases

The same release workflow can also publish the package to Nexus Mods. Configure
these repository settings first:

```text
Secret:   NEXUSMODS_API_KEY
Variable: NEXUSMODS_FILE_ID
Variable: NEXUSMODS_SPOTIFY_FILE_ID
Variable: NEXUSMODS_DIAGNOSTIC_FILE_ID
```

`NEXUSMODS_FILE_ID` is the existing Nexus Mods v3 mod-file ID used by
`POST /mod-files/{id}/versions`; it is not the game mod page number. Create the
Nexus Mods mod page and initial files manually. Use `NEXUSMODS_FILE_ID` for the
base Main file, `NEXUSMODS_SPOTIFY_FILE_ID` for the Spotify Optional file, and
`NEXUSMODS_DIAGNOSTIC_FILE_ID` for the Spotify diagnostic Optional file.

A normal `v*` tag updates both stable Nexus files independently and archives
the previous version of each. A tag ending in `-diagnose`, such as
`v2.0-diagnose`, builds only the Spotify diagnostic package and updates only
its diagnostic Nexus file. For an untagged diagnostic build, run the workflow
manually with `release_channel = diagnostic` and
`publish_nexusmods = true`.

## Credit

Thanks to plyrthn:
https://github.com/plyrthn/ds2_sd_card_music_player.git
