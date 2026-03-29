# DS2 音乐流程实测结论：函数职责（播放核心）

本文件只保留现有日志与 IDA MCP 能稳定支持的可靠结论，不记录计划、猜测、无法复现的单次现象或流水账。

## 已确认的函数职责

### `sub_140C14A00`

- 这是当前最稳定的“按索引取列表项”函数。
- 在当前样本里，它能稳定返回：
  - `entry`
  - `itemId2C`
  - `state30`
  - `flag35`
  - `trackObject`
  - `trackId`
  - `soundResource`
  - `trialSoundResource`
- 当前样本里：
  - `flag35=0`
  - `state30` 与 `index` 一一对应，覆盖 `0x00..0x18`
- 它不仅在页面进入时触发，也会在空闲光标移动、点击播放、点击下一首时再次触发。
- 在当前样本里，它也会在“播放中移动光标”时再次触发，而且仍然是整表枚举 `index=0..24`。
- 在“点击下一首”样本里，除了整表调用者 `DS2+0x1805332` 外，还观察到：
  - `DS2+0xC13315`
  - `DS2+0xC13332`
  - `DS2+0xC13381`
  这些调用者会对“下一首”的目标条目取项。
- 当前样本已经证明 `index` 与 `trackId` 不是同一个编号：
  - `index=0 -> trackId=38`
  - `index=9 -> trackId=3`
  - `index=17 -> trackId=28`
- 当前样本也已经证明，`sub_140C14A00` 返回的列表顺序不是固定不变的：
  - 较早的样本里，`index=9 -> trackId=66`
  - 较新的“下一首”样本里，`index=9 -> trackId=3`
- 在“切换播放列表回到 1”这组样本里，`sub_140C14A00` 返回的顺序与较新的“下一首”样本一致，例如：
  - `index=9 -> trackId=3`
  - `index=22 -> trackId=14`
  - `index=23 -> trackId=5`
  - `index=24 -> trackId=57`
- 当前样本已经证明，`sub_140C14A00` 枚举的是当前活动列表，而不是一个固定不变的全局顺序。
- 一组受控列表切换样本里，已观察到：
  - 当列表1只保留 1 首歌并切回列表1后，`index=0 -> trackId=5`
  - 紧接着切换到列表2后，`index=0 -> trackId=57`
- 这说明当前 hook 虽未直接记录“切换列表动作”本身，但已经可以通过 `sub_140C14A00` 返回条目的变化，间接区分不同活动列表内容。

### `sub_140C12580`

- 这是当前已确认的直接播放入口。
- 它能直接拿到：
  - `trackId`
  - `trackObject`
  - `soundResource`
  - `trialSoundResource`
- 当前样本里，直接播放的目标条目是：
  - `trackId=28`
  - `soundResource=0x1DB43550D50`
  - `trialSoundResource=0x1DB43553180`
- 在单曲活动列表 `trackId=57` 的点击播放样本里，直接播放目标条目是：
  - `index=0`
  - `trackId=57`
  - `soundResource=0x223C38A5AC0`
  - `trialSoundResource=0x223C38B56A0`
- 在一组真正重启进程后的新样本里（`pid` 从 `918256` 变为 `1046256`），再次对 `trackId=57` 执行直接播放时，还观察到：
  - `trackObject guid=FC BC 6F 3B 68 A8 42 43 A7 7F BD C6 D7 87 EB D1`
  - `soundResource guid=92 7F 62 16 36 3F 47 61 93 68 7E E2 D0 A3 13 A1`
  - `trialSoundResource guid=D1 A2 5F 9D 5B 74 4B DD AC 09 4C 95 1D 2F BB E7`
- 这组新进程样本里，条目地址和对象地址都变了，但上述 3 个 GUID 与旧进程样本保持一致。
- 因此，当前运行时样本已经支持：至少对 `trackId=57` 而言，`trackObject / soundResource / trialSoundResource` 的 GUID 在跨进程后仍然稳定。
- 在同一组新进程样本里，先播放第一首 `trackId=57`，再点击下一首切到第二首 `trackId=5` 时，还观察到：
  - `sub_140C12580(entry=0x3145D818038, trackId=5)`
  - `trackObject guid=09 76 3D 99 A2 D1 43 23 BC B5 C2 0D 7D DA 20 35`
  - `soundResource guid=F5 D5 4A 4A 8C 01 44 17 BF DC 6B F7 9C 0E D9 DA`
  - `trialSoundResource guid=56 82 27 78 F3 0E 4A D5 A7 6F CE EE DD 18 B4 9E`
- 这说明至少对第二首 `trackId=5` 而言，正式播放链上的资源标识在跨进程后同样保持稳定。

### `sub_140C15560`

- 这是当前已确认的试听入口。
- 它能直接拿到：
  - `trackId`
  - `trialSoundResource`
- 当前样本里，试听样本条目是：
  - `trackId=5`
  - `trialSoundResource=0x1DB43896E30`
- 在一组较新的试听样本里，它直接拿到：
  - `entry=0x51034C38000`
  - `trackId=57`
  - `trialSoundResource=0x2C6438B56A0`
- 在这组较新的样本里，进入试听前正式播放状态仍是：
  - `trackId=5`
  - `queueIndex=0`
  - `currentRuntime=0x51074792300`
- 随后 `preview.create` 使用的是该试听条目的 `trialSoundResource`，并把返回的运行时对象挂到 `trialRuntime`。
- 在这组较新的样本里，`sub_140C15560` 返回后：
  - `trackId=5`
  - `currentRuntime=0x51074792300`
  - `state2826=2`
  - `trialRuntime=0x5107472C080`
- 因此，试听建立不会覆盖当前正式播放对象，而是会额外挂接一条独立的 `trialRuntime`。
- 在同一进程内，对 `trackId=57` 执行“播放 -> 试听 -> 退出播放器再进来 -> 再试听”的受控样本里，两次试听都稳定命中：
  - `entry=0x5044A028000`
  - `trackId=57`
  - `trialSoundResource guid=D1 A2 5F 9D 5B 74 4B DD AC 09 4C 95 1D 2F BB E7`
- 在同一进程内，对 `trackId=5` 执行“移动到第二首 -> 试听”以及“保持在播放器内，再次移动到第二首 -> 试听”的受控样本里，两次试听都稳定命中：
  - `entry=0x5044A028038`
  - `trackId=5`
  - `trialSoundResource guid=56 82 27 78 F3 0E 4A D5 A7 6F CE EE DD 18 B4 9E`
- 因此，当前运行时样本已经支持：至少在同一进程内，同一可见条目的试听资源标识表现稳定。
- 在一组真正重启进程后的新样本里（`pid` 从 `918256` 变为 `1046256`），当第一首 `trackId=57` 正在正式播放、且光标选中第二首时，试听仍稳定命中：
  - `entry=0x3145D818038`
  - `trackId=5`
  - `trackObject guid=09 76 3D 99 A2 D1 43 23 BC B5 C2 0D 7D DA 20 35`
  - `trialSoundResource guid=56 82 27 78 F3 0E 4A D5 A7 6F CE EE DD 18 B4 9E`
- 这组新进程样本里，条目地址和对象地址都变了，但 `trackObject guid` 与 `trialSoundResource guid` 与旧进程样本保持一致。
- 因此，当前运行时样本已经支持：至少对 `trackId=5` 的试听链而言，相关资源标识在跨进程后仍然稳定。

### `sub_140C155F0`

- 这是当前已确认的试听槽清理点。
- 它会出现在：
  - 空闲光标移动
  - 已有试听对象时再次试听
  - 已有试听对象时开始正式播放
  - 点击下一首后的切歌收尾
- 当它命中时：
  - 如果此前存在试听对象，后面会看到 `preview.destroy`
  - 如果此前不存在试听对象，`currentTrialRuntimeObject=0x0`
- 在当前样本里，它不仅会出现在空闲光标移动时，也会出现在“播放中移动光标”时。
- 在当前“播放中移动光标”的样本里，它返回后的状态保持为当前播放状态，而不是空闲状态：
  - `state1910=2`
  - `state1911=1`
  - `trackId=38`
  - `value1928=28`
  - `queueIndex=0`
  - `currentRuntime=0x2881893EE00`
  - `state2826=0`
  - `trialRuntime=0x0`
- 在两组“点击下一首”样本里，它都出现在 `advance.current.create` 之后，并且返回后的状态就是新的切歌完成态。

### `sub_140C14CB0`

- 这是当前已确认的“销毁当前正式播放对象”的核心入口，已出现在“点击下一首”和“退出游戏”样本里。
- 它命中时记录到的 `currentRuntimeObject`，会与后续 `play.current.destroy` 的销毁对象一致。
- 当前已观察到的样本包括：
  - `currentRuntimeObject=0x28818937E00`
  - `currentRuntimeObject=0x2881893EE00`
- 在退出游戏样本里，它会在一批 `music-flow.other` 队列对象销毁之后，再销毁 `currentRuntimeObject`，并把 `manager` 状态收束回空闲态：
  - `state1910=0`
  - `trackId=0`
  - `currentRuntime=0x0`
- 在一组播放列表 `[5,20]` 的“下一首”样本里，它返回后还观察到：
  - `trackId=5`
  - `queueIndex=1`
  - `currentRuntime=0x0`
  - `state2826=1`
- 这说明在该切歌样本里，`sub_140C14CB0` 返回时旧的当前播放对象已经被销毁，`queueIndex` 已切到目标位置，但新曲目的 `trackId/currentRuntime` 还没有最终落稳。

### `sub_140AC5210`

- 这是当前已确认的运行时对象创建点。
- 在当前样本里，它已经稳定出现在 5 类已命名路径中：
  - `play.current.create`
  - `advance.current.create`
  - `preview.create`
  - `play.queue.primary.create`
  - `play.queue.trial.create`
- 当前样本里：
  - `play.current.create` / `advance.current.create` / `preview.create` 返回时，`flags180=50`
  - `play.queue.primary.create` / `play.queue.trial.create` 返回时，`flags180=51`
- 当前样本里，所有这些创建样本都满足：
  - `sourceObject170=0x0`
  - `state134=1`
  - `state1E0=0`
  - `state245=0`
  - `ref2BA=0`

### `sub_140AC5320`

- 这是当前已确认的运行时对象销毁点。
- 在当前样本里，它已经稳定出现在 3 类已命名路径中：
  - `play.current.destroy`
  - `preview.destroy`
  - `music-flow.other`
- 退出游戏样本里，`music-flow.other` 会先批量销毁一组队列相关的 `runtimeObject`；这些对象地址能与同一次播放链里先前创建的 `play.queue.primary.create / play.queue.trial.create` 结果一一对应。
- 当前样本里，`play.current.destroy` / `preview.destroy` 这两类销毁样本都观察到：
  - `flags180=32`

### `sub_140C12780`

- 这是当前已确认的播放状态切换点之一。
- 当前已分析到的样本都发生在正式播放建立或切歌完成之后。
- 这些已分析样本都出现相同的状态变化：
  - `state1910: 1 -> 2`
- 当前样本里，它不会替换 `currentRuntime`，而是保留当时已经建立好的正式播放对象。
- 当前新增样本里还观察到：
  - 当新曲目为 `trackId=3` 时，`arg2=0xD`
  - 此时：
    - `value1928=38`
    - `queueIndex=9`
    - `currentRuntime=0x28818936580`
- 在一组较新的按钮样本里，还观察到：
  - 切歌完成后当前状态为 `trackId=20`
  - `value1928=5`
  - `queueIndex=1`
  - `currentRuntime=0x51074720E00`
  - 随后 `sub_140C12780` 仍只表现为 `state1910: 1 -> 2`
- 这说明它至少也会作为“在不替换当前播放对象的前提下，对现有正式播放状态做切换”的路径出现。
- 在一组“换菜单 -> 播放 -> 手动暂停”的受控样本里，还观察到：
  - `sub_141808720`
  - `sub_140C12780(caller=0x7FF70C9B8D81 / DS2+0x1808D81)`
  - 未出现 `sub_140C12490`
  - 未出现 `sub_140C12580`
  - 未出现 `sub_140AC5210`
  - 未出现 `sub_140C12920`
- 同一组样本里，`sub_140C12780` 前后：
  - `trackId=5`
  - `queueIndex=0`
  - `currentRuntime=0x4A48B4D8C00`
  仅 `state1910: 1 -> 2`
- 这说明在至少一组手动暂停样本里，`sub_140C12780` 对应的是“对现有 currentRuntime 做状态切换”的 UI 路径，而不是重新选曲、重建播放对象或进入 `sub_140C12920`。

### `sub_140C12920`

- 当前已分析到的样本都出现在：
  - 点击播放
  - 点击下一首
- 这些已分析样本里 `manager` 都是 `0x0`，前后 `managerState` 都是 `unreadable`。
- 但这些已分析样本都能和 `sub_140C14A00` 返回的目标条目对上：
  - 点击播放样本里，`arg1=0x1DB43550530`，与 `index=17` 的 `trackObject` 一致
  - 点击下一首样本里，`arg0=0x1DB43550620`，与 `index=0` 的 `trackObject` 一致
  - 另一组点击下一首样本里，`arg0=0x1DB4386F0B0`，与 `index=9` 的 `trackObject` 一致
  - 同一组样本里，`arg1=0x3`，与 `index=9` 的 `trackId=3` 一致
  - 在一组播放列表 `[5,20]` 的“下一首”样本里，`arg0=0x2C6438662A8`，与目标 `index=1` 的 `trackObject` 一致
  - 同一组样本里，`arg1=0x14`，与目标 `index=1` 的 `trackId=20` 一致
- 因此，在“下一首”路径里，`sub_140C12920` 能直接拿到目标曲目的 `trackObject + trackId`。
- 在一组“换菜单 -> 播放 -> 手动暂停”的受控样本里，后续手动暂停只命中了 `sub_141808720 -> sub_140C12780`，未观察到 `sub_140C12920`。
- 因此，当前样本已经可以把“手动暂停路径”和“会进入 `sub_140C12920` 的另一类按钮/切歌路径”区分开。
- 在同一进程内，“当前正式播放对象已经存在时，再次点击当前曲目的播放按钮”的多组样本里，还观察到：
  - 只命中 `sub_141808720 -> sub_140C12920`
  - 未出现 `sub_140C12490`
  - 未出现 `sub_140C12580`
  - 未出现 `sub_140AC5210(play.current.create)`
- 这些样本后续并未观察到新的正式播放对象创建；至少有一组样本还能看到 `currentRuntime` 保持原地址不变。
- 因此，当前不能把 `sub_140C12920` 本身视为“新的正式资源加载已发生”的证据；它至少还会作为“对现有正式播放状态做操作/恢复”的路径出现。

### `sub_140C12AC0`

- `sub_140C12AC0(manager, indexOrSentinel, arg3)` 会先按 `sub_140C14C50` 把负值索引收束成有效位置。
- 当 `manager+6436` 上仍有当前 `trackId` 时，它会先调用 `sub_140C7EDA0(currentTrackId, targetIndex)`；当 `manager+6416 == 1` 时还会进一步调用 `sub_140C15350`。
- 只要当前播放状态 `manager+6416 != 0`，它就会：
  - 视情况把 `manager+6436` 复制到 `manager+6440`
  - 清零 `manager+6436`
  - 调用 `sub_140C13660(manager, 0)`
  - 调用 `sub_140C14CB0(manager)`
  - 清零 `manager+10276`
- 在 IDA 里，`sub_141808AE0` 会在“当前曲目已不再能通过 `sub_140C14BE0` 定位到可见条目”时调用它。
- 在一组“列表3中只有 `trackId=57`，先播放，再把它从列表3移除”的受控样本里，还观察到：
  - 进入 `sub_140C12AC0` 前，`sub_140C10E20.after` 已经把当前可见条目与当前条目都同步成 `state30=-1 / entryState48=-1`
  - `sub_140C12AC0.before` 时仍是 `state1910=1 / trackId=57 / currentRuntime!=0`
  - 随后先批量销毁 queue runtime（`callerTag=music-flow.other`）
  - 再通过 `sub_140C14CB0` 销毁当前播放对象
  - `sub_140C12AC0.after` 时收束到 `state1910=0 / trackId=0 / value1928=57 / currentRuntime=0`
- 这组运行时样本强烈支持：`sub_140C12AC0` 确实是“当前正在播放的曲目被移出有效列表后的清理入口”。
- 因此，它更像“当前播放曲目被移出当前有效列表后的清理入口”。

### `sub_140C12F80`

- `sub_140C12F80` 直接使用 `qword_14619C428` 作为 manager。
- 它会按 `manager+6417` 的播放模式推进当前位置：
  - 直接模式下推进 `manager+6448`
  - 洗牌相关模式下推进 `manager+6504`，再通过 `manager+6496` 映射回 `manager+6448`
- 推进位置后，它会读取 `sub_140C14A00(manager, manager+6448)` 对应的目标条目。
- 当目标条目与当前 `manager+6436` 不同，且条目上存在正式 `soundResource` 时，它会在需要时先 `sub_140C14CB0` 销毁旧对象，再用 `sub_140AC5210` 创建新的当前播放对象。
- 在一组真正重启进程后的新样本里，当前活动列表为 `[57,5]`，先播放第一首后再点击下一首时，还观察到：
  - `sub_140C14A00(index=1) -> trackId=5`
  - 随后 `sub_140C12580(entry=第二首)` 直接拿到第二首条目
  - `sub_140C14CB0` 先销毁第一首的 `currentRuntime`
  - 随后通过 `sub_140AC5210(callerTag=play.current.create)` 创建第二首的新正式播放对象
  - 收束状态为 `trackId=5 / value1928=57 / queueIndex=1 / currentRuntime!=0`
- 因此，当前运行时样本已经支持：`sub_140C12F80` 在“下一首”路径里并不只会落到 `advance.current.create`，也可能通过 `sub_140C12580 -> play.current.create` 完成正式切换。
- 它的 callers 已确认包括：
  - `sub_141809140`
  - `sub_1416E35C0`
- 因此，它是“向前推进当前播放位置并在需要时切到下一首”的核心实现点。

### `sub_140C132F0`

- `sub_140C132F0` 也直接使用 `qword_14619C428` 作为 manager。
- 它与 `sub_140C12F80` 结构同构，但会按 `manager+6417` 的播放模式向后收束当前位置：
  - 直接模式下回退 `manager+6448`
  - 洗牌相关模式下回退 `manager+6504`，再经 `manager+6496` 映射回 `manager+6448`
- 之后它同样会读取 `sub_140C14A00(manager, manager+6448)` 对应的目标条目，并在需要时切换当前播放对象。
- 它的 callers 已确认包括：
  - `sub_141809260`
  - `sub_1416E36E0`
- 因此，它是“向后回退当前播放位置并在需要时切到上一首”的核心实现点。
