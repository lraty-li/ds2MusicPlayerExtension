# PCM 捕获验证扩展

这是一个独立的 Chrome 116+ Manifest V3 扩展，用于验证 Spotify Web
Playback SDK 产生的受保护音频能否被 `chrome.tabCapture` 读到。

它只在 AudioWorklet 中累计统计量，不保存 PCM、不把 PCM 发送到网络，也不连接
游戏。开始 `tabCapture` 后原标签页会静音；本 PoC 不把捕获流接回扬声器，
因为最终链路应由游戏成为唯一可听输出。

## 使用

1. 在 `chrome://extensions` 开启“开发者模式”。
2. 选择“加载已解压的扩展程序”，指向本目录。
3. 打开 Spotify SDK 宿主页并开始播放。
4. 在该标签页点击扩展图标开始捕获；再次点击停止。

捕获开始后，扩展图标 badge 每秒更新：

- `PCM`：本统计窗口出现至少一个非零样本。
- `SIL`：本统计窗口的样本全部为零。
- `ERR`：捕获或音频图启动失败；鼠标悬停可查看错误。

## 页面事件

扩展会在开始、每秒统计、停止和错误时，向被捕获页面的 MAIN world 派发：

```js
window.addEventListener("ds2-spotify-capture-metrics", (event) => {
  console.log(event.detail);
});
```

`event.detail` 的字段固定为：

```js
{
  active,
  sampleRate,
  channels,
  rms,
  peak,
  nonzeroRatio,
  frames,
  elapsedMs,
  nonFiniteSamples,
  timestamp,
  audioContextState,
  error
}
```

`rms`、`peak` 和 `nonzeroRatio` 均为原始统计值，扩展不替宿主页设置“有效
PCM”的幅度阈值。`frames` 是当前统计窗口处理的音频帧数，`elapsedMs` 是
本次捕获的累计运行时间。Worklet 会在替换异常值之前统计
`nonFiniteSamples`；只要它非零，`error` 就会明确说明异常样本数。
