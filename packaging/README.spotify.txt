DS2MusicPlayer Spotify edition

English

This edition starts a hidden Edge WebView2 process group for Spotify Connect.
It added about 400 MiB of private committed memory on the test system; the
exact cost varies. Install the base edition if Spotify Connect is not needed.

1. Close the game.
2. Copy the contents of the DS2MusicPlayer folder into the game root.
   The game root is the folder that contains the game executable.
3. Expected layout after copying:
  <GameRoot>\version.dll
  <GameRoot>\scripts\Ds2MusicPlayerExtend.asi
  <GameRoot>\scripts\ds2_dll_music_resource.dll
  <GameRoot>\scripts\ds2_jacket_bc7e.dll
  <GameRoot>\scripts\DS2SpotifyHelper\DS2SpotifyWebView2Helper.exe
  <GameRoot>\scripts\DS2SpotifyHelper\config.json
  <GameRoot>\scripts\DS2SpotifyHelper\web\
4. Spotify Premium and a personal Spotify Developer App are required.
   Sign in at https://developer.spotify.com/dashboard, create an app, and
   select Web Playback SDK when asked which API/SDK it will use.
5. In the app settings, add this exact Redirect URI and save:
   https://appassets.example/index.html
6. Copy the public Client ID into
   scripts\DS2SpotifyHelper\config.json:
   {
     "spotifyClientId": "your 32-character Client ID",
     "proxyServer": ""
   }
   Never put the Client Secret in this file. Development Mode apps have an
   authorized-user limit; each user should normally create their own app.
7. Start the game. The helper window appears for the first PKCE authorization.
   After authorization, select "Death Stranding 2" in Spotify's device picker.
   Later launches keep the helper outside the visible desktop and task switcher.
8. Optional browser extension: open the Chrome/Edge extensions page, enable
   Developer mode, choose Load unpacked, and select browser-extension.
   WAIT means the extension is waiting for the game plugin; PCM means audio is
   streaming. Clicking the icon starts or stops streaming; game pause/resume
   controls the active source.
9. For NetEase Cloud Music, manually start playback in the page once before
   using game-side pause/resume. Chrome can reject a cold play request until
   the page has received user interaction.
10. The browser extension and Spotify Connect may stay connected together. The
   most recent explicit playback action owns the game stream; switching sources
   pauses the previous one. Automatic reconnect does not take ownership.
   Spotify controls do not change the game player's state.
11. The helper uses the Windows system proxy by default. To force a proxy, edit
   scripts\DS2SpotifyHelper\config.json and set proxyServer, for example
   "http://127.0.0.1:7890".

中文

DS2MusicPlayer Spotify 版

此版本会为 Spotify Connect 启动隐藏的 Edge WebView2 进程组。测试机上约
增加 400 MiB private commit，实际占用会随环境变化。不需要 Spotify Connect
时请安装基础版，以避免这部分常驻内存开销。

1. 关闭游戏。
2. 将 DS2MusicPlayer 文件夹内的内容复制到游戏根目录。
   游戏根目录是包含游戏 exe 的目录。
3. 复制后目录应为：
  <游戏根目录>\version.dll
  <游戏根目录>\scripts\Ds2MusicPlayerExtend.asi
  <游戏根目录>\scripts\ds2_dll_music_resource.dll
  <游戏根目录>\scripts\ds2_jacket_bc7e.dll
  <游戏根目录>\scripts\DS2SpotifyHelper\DS2SpotifyWebView2Helper.exe
  <游戏根目录>\scripts\DS2SpotifyHelper\config.json
  <游戏根目录>\scripts\DS2SpotifyHelper\web\
4. Spotify 版需要 Spotify Premium 和用户自己的 Spotify Developer App。
   登录 https://developer.spotify.com/dashboard，创建 App，并在询问使用
   哪个 API/SDK 时选择 Web Playback SDK。
5. 在 App 设置中精确添加以下 Redirect URI 并保存：
   https://appassets.example/index.html
6. 将公开的 Client ID 写入
   scripts\DS2SpotifyHelper\config.json：
   {
     "spotifyClientId": "你的 32 位 Client ID",
     "proxyServer": ""
   }
   不要填写 Client Secret。Development Mode App 有授权用户数量限制，
   每位用户通常应创建自己的 App。
7. 启动游戏。首次运行会显示 helper 窗口以完成 PKCE 授权；授权后在
   Spotify 的设备选择器中选择“Death Stranding 2”。后续启动时 helper
   会移到可见桌面之外，并从任务栏和 Alt-Tab 隐去。
8. 可选浏览器扩展：打开 Chrome/Edge 扩展管理页，启用开发者模式，
   选择“加载已解压的扩展”，然后选择 browser-extension 文件夹。
   WAIT 表示等待游戏插件，PCM 表示正在推流；点击图标开始或停止推流，
   游戏内暂停/恢复控制当前来源。
9. 网易云音乐：使用游戏内暂停/恢复前，请先在网页内手动播放一次。
   Chrome 可能会拒绝没有用户交互的冷启动播放请求。
10. 浏览器扩展与 Spotify Connect 可以同时保持连接。最后一次明确的播放
   动作取得游戏音频输入；切换来源时会暂停之前的来源，自动重连不会抢占。
   Spotify 侧控制不会改变游戏播放器状态。
11. helper 默认使用 Windows 系统代理。如需强制代理，请编辑
   scripts\DS2SpotifyHelper\config.json 的 proxyServer，例如
   "http://127.0.0.1:7890"。

Credits

version.dll comes from Ultimate ASI Loader:
https://github.com/ThirteenAG/Ultimate-ASI-Loader
