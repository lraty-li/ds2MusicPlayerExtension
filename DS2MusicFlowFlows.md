# DS2 音乐流程实测结论：流程总览

本文件只保留现有日志与 IDA MCP 能稳定支持的可靠结论，不记录计划、猜测、无法复现的单次现象或流水账。

## 已确认的流程

### 页面进入与光标移动

- 进入播放器页面但尚未操作时，会触发 `sub_140C14A00` 对当前活动列表取项。
- 在当前 25 项活动列表样本里，这表现为 `index=0..24` 的整表枚举。
- 在当前单曲活动列表样本里，只观察到 `index=0`。
- 在当前 hook 集合下，进入播放器页面样本里还观察到一次 `sub_141808C00`，它出现在两次 `sub_140C14A00` 取项之间。
- 这条整表枚举在当前样本里由固定调用者触发：
  - `caller=DS2+0x1805332`
- 在当前 hook 集合下，空闲状态下光标来回移动时，会先命中 `sub_141808C00`，随后命中 `sub_140C155F0`，然后再次触发 `sub_140C14A00` 对当前活动列表取项。
- 在当前 25 项活动列表样本里，这一步表现为 `index=0..24` 的整表枚举。
- 在当前单曲活动列表样本里，这一步只观察到 `index=0`。
- 这些空闲光标移动样本里的管理器状态稳定为：
  - `state1910=0`
  - `state1911=1`
  - `trackId=0`
  - `value1928=0`
  - `queueIndex=17`
  - `currentRuntime=0x0`
  - `state2826=0`
  - `trialRuntime=0x0`
- 在“已经存在正式播放对象，且当前播放为 `trackId=38`”的样本里：
  - 将光标移动到第一首
  - 将光标向右移动
  这两种操作都只观察到同一条流程：
  - `sub_140C155F0`
  - `sub_140C14A00` 对 `index=0..24` 的整表枚举
- 在上面这两组样本里，`sub_140C155F0 managerState.after` 完全相同：
  - `state1910=2`
  - `state1911=1`
  - `trackId=38`
  - `value1928=28`
  - `queueIndex=0`
  - `currentRuntime=0x2881893EE00`
  - `state2826=0`
  - `trialRuntime=0x0`
- 在当前这些光标移动样本里，尚未观察到会随着“移动到第一首”与“向右移动”而改变的已记录字段值。
- 在“从第 1 首移到第 2 首，再移回第 1 首”的样本里，当前已记录字段仍不足以区分这两个方向；两次移动里记录到的 `sub_141808C00` 与 `sub_140C155F0` 关键字段都相同。
- 在单曲活动列表样本里，`sub_141808C00` 记录到的 `uiSelectedIndex=1` 与 `uiSelectedIndex=0` 最终都落到同一个 `sub_140C14A00(index=0)` 条目。

### 直接播放

- “空闲状态下直接点击播放”时，会稳定命中 `sub_140C12580`。
- 当前已观察到的直接播放链为：
  - `sub_140C12580`
  - `sub_140AC5210 [play.current.create]`
  - `sub_140C155F0`
  - 如果此前存在试听对象，还会出现 `sub_140AC5320 [preview.destroy]`
  - 随后出现多次 `sub_140AC5210 [play.queue.primary.create / play.queue.trial.create]`
- 直接播放完成后的已观察状态为：
  - `state1910=5`
  - `state1911=1`
  - `trackId=28`
  - `value1928=0`
  - `queueIndex=17`
  - `currentRuntime` 指向新的正式播放对象
  - `state2826=0`
  - `trialRuntime=0x0`
- 但当当前正式播放对象已经存在，且再次对当前曲目按下播放时，不一定会重新走这条“正式对象创建链”。
- 当前已观察到多组样本只命中：
  - `sub_141808720`
  - `sub_140C12920`
- 这些样本没有再出现 `sub_140C12580` 或 `play.current.create`；因此它们更像“现有正式播放状态的操作/恢复”，而不是新的正式资源加载。

### 试听

- “点击试听 / 样本播放按钮”时，会稳定命中 `sub_140C15560`。
- 当前已观察到的试听链为：
  - `sub_140C15560`
  - 如果此前已有试听对象，则先出现 `sub_140C155F0`
  - 如果此前已有试听对象，则随后出现 `sub_140AC5320 [preview.destroy]`
  - `sub_140AC5210 [preview.create]`
- 试听对象建立后：
  - `trialRuntime` 指向新的试听对象
  - `currentRuntime` 不会因此被覆盖
- 当前运行时样本还支持：试听目标跟随当前选中的可见条目，而不是当前正式播放曲目。
- 空闲状态下建立试听对象后，已观察到：
  - `state1910=0`
  - `state1911=1`
  - `trackId=0`
  - `value1928=0`
  - `queueIndex=17`
  - `currentRuntime=0x0`
  - `state2826=2`
  - `trialRuntime` 指向新的试听对象

### 下一首

- “点击下一首”时，会稳定命中 `sub_140C14CB0`。
- 当前已观察到的切歌链为：
  - 先通过 `sub_140C14A00` 对目标 `index` 取项
  - `sub_140C14CB0`
  - `sub_140AC5320 [play.current.destroy]`
  - 随后会进入新的正式对象创建链
- 当前已观察到两种“下一首”正式创建分支：
  - `sub_140AC5210 [advance.current.create]`
  - `sub_140C12580 -> sub_140AC5210 [play.current.create]`
- 在当前样本里，下一首之前的正式播放对象是：
  - `currentRuntimeObject=0x28818937E00`
- 下一首新建出来的正式播放对象是：
  - `runtimeObject=0x2881893EE00`
- 当前样本里，“下一首”选择的目标条目是 `index=0`，对应：
  - `trackId=38`
  - `soundResource=0x1DB43553F30`
  - `trialSoundResource=0x1DB43551780`
- 在当前样本里，`sub_140C155F0` 返回后，切歌完成态为：
  - `state1910=5`
  - `state1911=1`
  - `trackId=38`
  - `value1928=28`
  - `queueIndex=0`
  - `currentRuntime=0x2881893EE00`
  - `state2826=0`
  - `trialRuntime=0x0`
- 在一组真正重启进程后的新样本里，活动列表为 `[57,5]`，先播放第一首再点下一首时，还观察到：
  - 目标条目为 `index=1 -> trackId=5`
  - 切歌路径落到 `sub_140C12580 -> sub_140AC5210 [play.current.create]`
  - 切歌完成态为 `trackId=5 / value1928=57 / queueIndex=1 / currentRuntime!=0`
- 另一组“点击下一首”样本里，切歌开始前先由这些调用者对目标条目取项：
  - `DS2+0xC13315`
  - `DS2+0xC13332`
  - `DS2+0xC13381`
- 这一组样本里，这几次取项都命中：
  - `index=9`
  - `trackId=3`
  - `soundResource=0x1DB438AA7F0`
  - `trialSoundResource=0x1DB438A6D60`
- 这一组样本里，被销毁的旧正式播放对象是：
  - `currentRuntimeObject=0x2881893EE00`
- 这一组样本里，新建的正式播放对象是：
  - `runtimeObject=0x28818936580`
- 这一组样本里，`sub_140C155F0` 返回后的切歌完成态为：
  - `state1910=5`
  - `state1911=1`
  - `trackId=3`
  - `value1928=38`
  - `queueIndex=9`
  - `currentRuntime=0x28818936580`
  - `state2826=0`
  - `trialRuntime=0x0`
