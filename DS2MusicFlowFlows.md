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
- 即使试听目标与当前正式播放曲目是同一首，也仍会新建独立的 `trialRuntime`，而不是复用 `currentRuntime`。
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
- 在一组“光标停在第二首，先试听第二首，再点击正式播放”的连续样本里，还观察到：
  - 试听阶段建立的是第二首 `trackId=5` 的 `trialRuntime`
  - 点击正式播放后，会先触发 `preview.destroy` 销毁该试听对象
  - 随后正式播放并没有直接转正第二首，而是从播放列表队列头开始，为第一首 `trackId=57` 建立 `currentRuntime`
- 这说明“试听”与“正式播放”在当前实现里仍是两条独立入口；试听成功并不会把当前选中条目直接提升成正式播放对象。

## 对额外音乐加载最相关的已确认收束

- 当前运行时日志已经确认，和“把额外音乐接进游戏内播放器”最相关的资源入口有两条：
  - 正式播放入口：`sub_140C12580`，它直接拿到条目的 `soundResource`
  - 试听入口：`sub_140C15560`，它直接拿到条目的 `trialSoundResource`
- 这两条入口随后都会汇合到同一个运行时对象创建点：
  - `sub_140AC5210`
- 因此，如果目标是让额外音乐同时支持“正式播放”和“试听”，当前最值得继续围绕的不是播放器 UI 本身，而是：
  - 谁向 `sub_140C12580 / sub_140C15560` 提供资源对象
  - 以及 `sub_140AC5210` 下游是谁真正把资源推进成可播放数据

### 更深层观察结果

- 本轮在“主菜单空闲 + 真实音乐操作”两类样本里都没有观察到 `sub_1426E7CC0` 的运行时命中；当前只能确认 hook 已成功安装，尚不能把它视为已被音乐链实际触发的证据。
- `sub_142695420` 在当前版本里仍然没有挂上，初始化日志稳定表现为 `signature not found`。
- `sub_1426B00A0` 作为更底层的 Wwise 事件投递层，上一轮已证实在主菜单空闲时会高频触发，且当前样本里都没有 `externalSourceCount>0`；因此它已被降级为辅助观察点，而不是下一轮的主要突破口。
- 因此，若下一轮继续朝“额外歌曲接入”推进，优先级应回到 IDA 侧，先修正 `sub_142695420` 的实际落点或替代签名，再决定是否继续做更深层运行时日志。

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
- 同一组样本里还可直接串起来看到：
  - `sub_140C12F80`
  - `sub_140C14CB0`
  - `sub_140AC5320 [play.current.destroy]`
  - `sub_140C12580`
  - `sub_140AC5210 [play.current.create]`
- 因此，当前“手动下一首”至少存在一条完整可重现的路径，是先销毁旧 `currentRuntime`，再复用正式播放创建链，而不是进入单独的 `advance.current.create` 分支。
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
