# Spotify Web Playback SDK → tabCapture 最小验证

这个 PoC 完全脱离游戏，只验证两件事：

1. Spotify Web Playback SDK 能否在 Chrome 中注册一个可选择的 Connect 设备。
2. 该设备播放的真实 EME 音频能否被 `chrome.tabCapture` 持续取得为非零 PCM。

音频只在 AudioWorklet 中计算 RMS、峰值和非零比例，不会保存、上传或发送到游戏。

## 前置条件

- Spotify Premium。
- Chrome 116 或更高版本。
- Spotify Developer Dashboard 中的应用：
  - 启用 Web Playback SDK。
  - Redirect URI 添加 `http://127.0.0.1:8765/`。
  - Development Mode 下，将测试账号加入 Users Management allowlist。

Redirect URI 必须逐字符匹配，包括端口和末尾 `/`。不要使用 `localhost`。

## 静态校验

在本目录运行：

```powershell
.\build.ps1
```

脚本验证必需文件、Manifest V3、JavaScript 语法和每个代码文件不超过 300 行。

## 加载捕获扩展

1. 打开 `chrome://extensions`。
2. 启用开发者模式。
3. 点击“加载已解压的扩展程序”。
4. 选择本目录下的 `capture-extension`。

该扩展没有 popup。点击工具栏图标即开始或停止捕获当前标签页：

- `PCM`：当前窗口检测到非零 PCM。
- `SIL`：捕获正常，但当前窗口接近静音。
- `ERR`：捕获失败。

## 启动本地页面

```powershell
.\start.ps1
```

打开：

```text
http://127.0.0.1:8765/
```

如果指定了其他端口，必须同步修改 Developer Dashboard 中的 Redirect URI。

## 验证步骤

1. 输入并保存 Spotify Client ID。
2. 点击“在本机浏览器授权 Spotify”，完成 PKCE 授权。
3. 点击“启用 Connect 设备”。
4. 等待页面收到非空 Device ID。
5. 在手机或桌面 Spotify 的设备列表中选择：

   ```text
   Death Stranding 2 Web SDK PoC
   ```

6. 播放一首完整正式曲目，确认页面状态为“播放中”。
7. 保持 PoC 页面为当前标签页，点击捕获扩展图标。
8. 捕获开始后标签页不再从本机扬声器出声是 Chrome 的预期行为。

## 通过标准

页面显示“通过：持续捕获到 48 kHz 非零 PCM”只代表最短的关键链路成立。
完整验证还应依次满足：

1. 完整曲目连续播放至少 60 秒，指标持续更新且不是全零。
2. 从 Spotify 暂停后 RMS 降到接近零。
3. 恢复后非零 PCM 再次出现。
4. 切换另一首完整曲目后，SDK 元数据改变且 PCM 继续更新。
5. `sampleRate` 为 `48000`，声道数为 1 或 2。

如果存在持续非零 PCM，但采样率不是 48 kHz，应记录为：

- EME/tabCapture 可行；
- 后续接入游戏前必须显式重采样。

仅看到 Connect 设备、播放进度或 `MediaStreamTrack=live` 都不足以证明捕获成功。

## 本地数据

页面只在当前 origin 的浏览器 Local Storage 中保存：

- Client ID；
- access token；
- refresh token；
- token 过期时间。

不保存 Client Secret。点击“清除本地授权”会删除本地 token。

## 官方资料

- [Spotify PKCE](https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow)
- [Spotify Web Playback SDK](https://developer.spotify.com/documentation/web-playback-sdk/reference)
- [Spotify Redirect URI](https://developer.spotify.com/documentation/web-api/concepts/redirect_uri)
- [Chrome tabCapture](https://developer.chrome.com/docs/extensions/reference/api/tabCapture)
