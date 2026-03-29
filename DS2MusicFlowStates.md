# DS2 音乐流程实测结论：状态字段与顺序样本

本文件只保留现有日志与 IDA MCP 能稳定支持的可靠结论，不记录计划、猜测、无法复现的单次现象或流水账。

## 已确认的状态字段

### `trackId`

- 空闲状态样本里为 `0`
- 直接播放完成态样本里为 `28`
- 下一首完成态样本里为 `38`
- 另一组“下一首”完成态样本里为 `3`
- 试听建立样本里，`sub_140C15560` 读到的试听条目 `trackId` 可以与正式播放 `trackId` 不同
- 当前样本已经证明 `trackId` 不是列表位置编号：
  - `index=0 -> trackId=38`
  - `index=9 -> trackId=3`
  - `index=17 -> trackId=28`
- 在“切换播放列表回到 1”的样本里，已再次观察到：
  - `index=9 -> trackId=3`
  - `index=22 -> trackId=14`
- 因此，`trackId` 不是当前播放列表里的排序位置。

### `currentRuntime`

- 正式播放建立后，`currentRuntime` 会等于 `play.current.create` 或 `advance.current.create` 返回的对象地址
- 空闲状态样本里，`currentRuntime=0x0`
- 试听建立不会覆盖 `currentRuntime`

### `trialRuntime`

- 试听建立后，`trialRuntime` 会等于 `preview.create` 返回的对象地址
- 试听销毁后，`trialRuntime=0x0`
- 进入正式播放且清理试听后，`trialRuntime=0x0`

### `state2826`

- 空闲状态样本里为 `0`
- 空闲状态下点击试听前，已观察到 `1`
- 试听建立完成后，已观察到 `2`
- 试听被清理后回到 `0`
- 下一首样本里，在 `sub_140C14CB0` 返回后曾观察到 `1`，在切歌完成态回到 `0`

### `value1928`

- 空闲状态样本里为 `0`
- 直接播放完成态样本里为 `0`
- 当前“下一首”完成态样本里为 `28`
- 另一组“下一首”完成态样本里为 `38`
- 在当前两组“下一首”完成态样本里，`value1928` 都等于上一首的 `trackId`：
  - 从 `28` 切到 `38` 后，`value1928=28`
  - 从 `38` 切到 `3` 后，`value1928=38`
- 在一组播放列表 `[5,20]` 的“下一首”样本里，还观察到：
  - 从 `5` 切到 `20` 后，`value1928=5`
- 因此，当前样本继续支持 `value1928` 更像“上一首的 trackId”。

### `state30`

- 在一组“把第四首歌加入播放列表1”的受控样本里，同一个目标条目 `trackId=20` 在加入前后出现了明显变化：
  - 通过 `sub_140C14910 / sub_140C149C0` 观察到时，`state30=0xFFFFFFFF`
  - 加入完成后通过 `sub_140C14A00(index=1)` 再观察到时，`state30=0x00000001`
- 在一组“空的播放列表3中加入第一首歌 `trackId=57`”的受控样本里，还观察到：
  - `sub_140C10E20.before` 时，同一个可见条目 `state30=0xFFFFFFFF`
  - `sub_140C10E20.after` 时，同一个可见条目 `state30=0x00000000`
- 在一组“把列表3里唯一的 `trackId=57` 移除”的受控样本里，还观察到完全相反的变化：
  - `sub_140C10E20.before` 时，同一个可见条目 `state30=0x00000000`
  - `sub_140C10E20.after` 时，同一个可见条目 `state30=0xFFFFFFFF`
- 在一组“先播放 `trackId=57`，再把它从列表3移除”的受控样本里，还观察到：
  - `sub_140C10E20.before.current` 时，当前条目 `state30=0x00000000`
  - `sub_140C10E20.after.current` 时，同一条目 `state30=0xFFFFFFFF`
  - 随后进入 `sub_140C12AC0` 做当前播放清理
- 结合已多次观察到的 `sub_140C14A00(index=n)` 返回条目通常满足 `state30=n`，当前受控样本支持：
  - `state30` 很像“当前活动列表中的位置”
  - `state30=-1` 很像“当前不在活动列表里”

### `entryState48`

- 在一组“空的播放列表3中加入第一首歌 `trackId=57`”的受控样本里，同一个可见条目在 `sub_140C10E20` 前后出现了：
  - `entryState48=-1 -> 0`
- 在一组“把列表3里唯一的 `trackId=57` 移除”的受控样本里，同一个可见条目又出现了：
  - `entryState48=0 -> -1`
- 在一组“先播放 `trackId=57`，再把它从列表3移除”的受控样本里，当前条目也出现了：
  - `entryState48=0 -> -1`
- 因此，当前运行时样本强烈支持：`entryState48` 与 `state30` 一样，也在表达“当前是否属于活动播放列表以及其位置”；当它为 `-1` 时，很像“当前不在活动播放列表里”。

### `queueIndex`

- 空闲状态样本里为 `17`
- 直接播放完成态样本里为 `17`
- 在当前两组“下一首”样本里，切歌完成后的 `queueIndex` 都等于本次目标条目的 `index`：
  - 目标 `index=0` 时，完成后 `queueIndex=0`
  - 目标 `index=9` 时，完成后 `queueIndex=9`
- 在一组播放列表 `[5,20]` 的“下一首”样本里，目标 `index=1` 时，完成后 `queueIndex=1`
- 当前样本已经支持把 `queueIndex` 视为“当前活动列表中的位置索引”。

## 已确认的列表顺序样本

当前日志已经证明，`sub_140C14A00(index)` 返回的顺序会变化，因此下面这些只应视为“已观察到的顺序样本”，而不是固定映射表。

- 较早样本里，已观察到：
  - `index=0 -> trackId=38`
  - `index=6 -> trackId=5`
  - `index=17 -> trackId=28`
- 较新的“下一首”样本里，已观察到：
  - `index=0 -> trackId=38`
  - `index=9 -> trackId=3`
  - `index=22 -> trackId=14`
  - `index=23 -> trackId=5`
  - `index=24 -> trackId=57`
- “切换播放列表回到 1”样本里，已观察到与上面完全一致的顺序片段：
  - `index=0 -> trackId=38`
  - `index=9 -> trackId=3`
  - `index=22 -> trackId=14`
  - `index=23 -> trackId=5`
  - `index=24 -> trackId=57`
- 受控列表切换样本里，已观察到：
  - 当列表1只保留 1 首歌并切回列表1后，`index=0 -> trackId=5`
  - 紧接着切换到列表2后，`index=0 -> trackId=57`
- 另一组受控列表切换样本里，还观察到：
  - 切到只含 `trackId=57` 的列表时，`index=0 -> entry=0x4E4FD428000`
  - 切回只含 `trackId=5` 的列表时，`index=0 -> entry=0x4E4FD428038`
- 这两个地址相差 `0x38`，与当前 56 字节条目步长一致，说明它们很像同一组列表槽位中的相邻两个条目。
