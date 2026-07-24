DS2MusicPlayer distribution

English

1. Close the game.
2. Copy the contents of the DS2MusicPlayer folder into the game root.
   The game root is the folder that contains the game executable.
3. Expected layout after copying:
  <GameRoot>\version.dll
  <GameRoot>\scripts\Ds2MusicPlayerExtend.asi
  <GameRoot>\scripts\ds2_dll_music_resource.dll
  <GameRoot>\scripts\ds2_jacket_bc7e.dll
  <GameRoot>\scripts\DS2SpotifyConnectBridge.dll
4. Browser extension:
   Open Chrome/Edge extensions page, enable Developer mode, choose Load
   unpacked, and select the browser-extension folder.
5. Open a browser tab that plays audio, click the extension icon, then start
   the game. WAIT means the extension is waiting for the game plugin; PCM
   means audio is streaming.
6. The extension is in controlled mode: clicking the icon only starts or stops
   streaming. Pause/resume is controlled by the in-game player state.
7. NetEase Cloud Music: manually start playback in the page once before using
game-side pause/resume. Chrome can reject a cold play request until the page
has received user interaction.
8. Spotify Connect: after the game starts, select "Death Stranding 2" in
Spotify's device picker. The bridge sends Spotify audio, track information,
and album art to the in-game special track. Do not use browser streaming and
Spotify Connect at the same time. Game pause/resume controls Spotify; Spotify
controls do not change the game player's state.

中文

1. 关闭游戏。
2. 将 DS2MusicPlayer 文件夹内的内容复制到游戏根目录。
   游戏根目录是包含游戏 exe 的目录。
3. 复制后目录应为：
  <游戏根目录>\version.dll
  <游戏根目录>\scripts\Ds2MusicPlayerExtend.asi
  <游戏根目录>\scripts\ds2_dll_music_resource.dll
  <游戏根目录>\scripts\ds2_jacket_bc7e.dll
  <游戏根目录>\scripts\DS2SpotifyConnectBridge.dll
4. 浏览器扩展：
   打开 Chrome/Edge 扩展管理页，启用开发者模式，选择“加载已解压的扩展”，
   然后选择 browser-extension 文件夹。
5. 打开一个正在播放音频的浏览器标签页，点击扩展图标，再启动游戏。
   WAIT 表示扩展正在等待游戏插件；PCM 表示正在推流。
6. 扩展处于受控模式：点击图标只负责开始或停止推流；暂停/恢复由游戏内
   播放器状态控制。
7. 网易云音乐：使用游戏内暂停/恢复前，请先在网页内手动播放一次。
Chrome 可能会拒绝没有用户交互的冷启动播放请求。
8. Spotify Connect：启动游戏后，在 Spotify 的设备选择器中选择
“Death Stranding 2”。桥接程序会把 Spotify 音频、曲目信息和专辑图发送到
游戏内的特殊曲目。浏览器推流与 Spotify Connect 不要同时使用；游戏内
暂停/恢复会控制 Spotify，但 Spotify 侧控制不会改变游戏播放器状态。

Credits

version.dll comes from Ultimate ASI Loader:
https://github.com/ThirteenAG/Ultimate-ASI-Loader
