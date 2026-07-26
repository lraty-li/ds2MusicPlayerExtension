DS2MusicPlayer base edition

English

1. Close the game.
2. Copy the contents of the DS2MusicPlayer folder into the game root.
   The game root is the folder that contains the game executable.
3. Expected layout after copying:
  <GameRoot>\version.dll
  <GameRoot>\scripts\Ds2MusicPlayerExtend.asi
  <GameRoot>\scripts\ds2_dll_music_resource.dll
  <GameRoot>\scripts\ds2_jacket_bc7e.dll
4. This base edition never starts the Spotify WebView2 helper. To use Spotify
   Connect, install the separately distributed Spotify edition instead.
   The Spotify helper added about 400 MiB of private committed memory on the
   test system; the exact cost varies by WebView2 version and machine state.
   This base edition avoids that resident memory overhead.
   If replacing the Spotify edition, an old scripts\DS2SpotifyHelper folder may
   remain. It will not run and may be deleted while the game is closed.
5. Browser extension:
   Open Chrome/Edge extensions page, enable Developer mode, choose Load
   unpacked, and select the browser-extension folder.
6. Open a browser tab that plays audio, click the extension icon, then start
   the game. WAIT means the extension is waiting for the game plugin; PCM
   means audio is streaming.
7. The extension is in controlled mode: clicking the icon only starts or stops
   streaming. Pause/resume is controlled by the in-game player state.
8. NetEase Cloud Music: manually start playback in the page once before using
   game-side pause/resume. Chrome can reject a cold play request until the page
   has received user interaction.

中文

DS2MusicPlayer 基础版

1. 关闭游戏。
2. 将 DS2MusicPlayer 文件夹内的内容复制到游戏根目录。
   游戏根目录是包含游戏 exe 的目录。
3. 复制后目录应为：
  <游戏根目录>\version.dll
  <游戏根目录>\scripts\Ds2MusicPlayerExtend.asi
  <游戏根目录>\scripts\ds2_dll_music_resource.dll
  <游戏根目录>\scripts\ds2_jacket_bc7e.dll
4. 基础版绝不会启动 Spotify WebView2 helper。如需 Spotify Connect，
   请改用单独发布的 Spotify 版。Spotify helper 在测试机上约增加
   400 MiB private commit；实际占用会随
   WebView2 版本和系统状态变化。基础版可避免这部分常驻内存开销。
   从 Spotify 版覆盖安装基础版时，旧的
   scripts\DS2SpotifyHelper 文件夹可能仍会保留；它不会运行，可在关闭游戏
   后手动删除。
5. 浏览器扩展：
   打开 Chrome/Edge 扩展管理页，启用开发者模式，选择“加载已解压的扩展”，
   然后选择 browser-extension 文件夹。
6. 打开一个正在播放音频的浏览器标签页，点击扩展图标，再启动游戏。
   WAIT 表示扩展正在等待游戏插件；PCM 表示正在推流。
7. 扩展处于受控模式：点击图标只负责开始或停止推流；暂停/恢复由游戏内
   播放器状态控制。
8. 网易云音乐：使用游戏内暂停/恢复前，请先在网页内手动播放一次。
   Chrome 可能会拒绝没有用户交互的冷启动播放请求。

Credits

version.dll comes from Ultimate ASI Loader:
https://github.com/ThirteenAG/Ultimate-ASI-Loader
