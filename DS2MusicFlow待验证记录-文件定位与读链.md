# DS2 音乐流程待验证记录：文件定位与读链

本页只保留“resolver 已命中之后，真实可听数据链到底落到哪里”的开放问题。
已经被运行日志确认的结论已移入主知识文件与流程总览，不再在本页重复展开。

## 当前仍需闭环的问题

- `sub_142693510 -> sub_1426C4120 -> sub_14206FEF0` 已经通过运行日志闭到提交节点，但 `sub_14206FEF0` 之后谁真正消费这些分块，仍未闭环。
- preview 与正式播放已确认都会命中 `sub_1426C4120`，但它们在更下游是否会再次分叉，仍需确认。
- 若最终替换点不是 resolver 返回阶段，也不是 `sub_14206FEF0` 本身，真实可听链会落到哪个更下游的读、流或解码完成点。
- `wemId / streamHandle / requestKey / fileToken / fileView` 这几套键之间如何转换，仍未闭环。

## 需要继续依赖 IDA 下钻的对象问题

- `WwiseWemResource` 的哪一段内嵌 stream 才是最小可改写边界。
- `sub_1426C4120` 里由 `streamObject + 0x30` 解出的 `WwiseWemResource*`，在后续哪些函数里继续被解引用。
- `sub_1426C4120` 读到的 `segment / segmentCtx / streamHandle`，哪一层才是真正的分块数据源选择边界。
- 是否存在一个比“伪造完整 `WwiseWemResource`”更轻的做法，只改指针、内嵌 stream 或分段来源就能让目录外部音乐接管当前曲目。

## 已获得的静态证据（尚未经过运行日志确认）

- `sub_1426C4120` 不是直接提交读请求；它会调用 `off_14407FB20` 的虚表 `+0x20`，当前落点是 `sub_14206A490`。
- `sub_14206A490` 会先调用 `sub_14206A1B0` 构造 `cache:streams/%x/%x/%x/%x/%s.%02x.stream` 路径，再把请求对象交给 `qword_14619D918` 当前设备对象的虚表 `+0x20`。
- 因而，`sub_1426C4120` 之后真正稳定的抽象边界不是某一个固定函数，而是：
  - `SubmitStreamCacheReadRequest`
  - `-> qword_14619D918`
  - `-> vftable + 0x20`
- 这一个虚表槽位当前只静态确认了两种具体实现：
  - `NXStorageReadDevice_SubmitRequest (sub_14206FEF0)`
  - `DecompressingReadDevice_SubmitRequest (sub_142073210)`
- `sub_14206A490` 传下去的请求结构已经带有足够具体的 I/O 字段：
  - `request + 0x00`：路径字符串指针
  - `request + 0x08`：请求键，初始常见值为 `-1`
  - `request + 0x10`：`segment + 0x10`
  - `request + 0x18`：`segmentBase + streamOffset`
  - `request + 0x20`：`segmentSize`
  - `request + 0x30`：完成回调
  - `request + 0x38`：回调上下文，静态上对应 `segment`
- `sub_140119CF0 -> sub_14206ED10` 会先创建 `NXStorageReadDevice` 并写入 `qword_14619D918`。
- `sub_1426E47A0` 在流媒体初始化阶段会检查 streaming graph；若图中存在对应条目，会调用 `sub_1420721B0` 构造 `DecompressingReadDevice`，并再次把 `qword_14619D918` 改成新的包装设备。
- 目前静态上只确认到这两处主路径会写 `qword_14619D918`：
  - 基础初始化时写入 `NXStorageReadDevice`
  - `sub_1426E47A0` 条件成立时写入 `DecompressingReadDevice`
- 除了 `sub_1426E47A0` 内部分配失败后的防御性写回，尚未看到第三个常规写点会把它改成别的设备类型。
- `sub_1420721B0` 构造 `DecompressingReadDevice` 时，会把旧的活动设备保存到 `device + 0x40`。
- 因而即使运行时当前设备已经切到 `DecompressingReadDevice`，该对象内部仍可能把某些请求继续转发到底层 `NXStorageReadDevice`。
- 目前更像“真实提交读请求入口”的下一个候选点是 `sub_14206FEF0`：
  - 它正是设备虚表 `+0x20` 这类读入口之一
  - 它会消费上面的请求结构，分配/复用 I/O operation slot，并把返回句柄写回调用方提供的输出地址
  - 已被确认会调用 `sub_14206E920` 复制请求对象，后续再进入设备内部排队
- `NXStorageReadDevice_SubmitRequest (sub_14206FEF0)` 在 `request + 0x08 == -1` 时，会用 `sub_1400CA170 -> sub_1420713E0` 按路径字符串查文件对象。
- 同一个提交点会把选中的文件对象写到 operation context 的 `+0x08`，并把 operation handle 写到 `+0x78`。
- `NXStorageReadDevice_AllocOpContext (sub_142071710)` 通过空闲表取回 operation slot；静态上已经能看出，返回 handle 的高 16 位直接对应 slot 索引。
- 因此在 `sub_14206FEF0` 返回后，可以用：
  - `slotIndex = (handle >> 16) & 0xFFFF`
  - `opContext = *(_QWORD *)(device + 0x200) + 144 * slotIndex`
  重新定位本次 operation context，并回读 `opContext + 0x08` 里的文件槽令牌。
- `NXStorageReadDevice_IoWorkerThread (sub_1420708D0)` 会收集待处理 operation context，并交给 `NXStorageReadDevice_QueueBatchReads (sub_1420704E0)`。
- `NXStorageReadDevice_QueueBatchReads (sub_1420704E0)` 会按“同一文件对象 + 连续偏移”合并请求，然后调用 `Win32ReadQueue_EnqueueRead (sub_1427FE5C0)`。
- `Win32ReadQueue_EnqueueRead (sub_1427FE5C0)` 写入的低层队列项已经明确带有：
  - 文件对象
  - 文件偏移
  - 输出缓冲
  - 读取长度
- 进一步静态确认：
  - `Win32ReadQueue_EnqueueRead` 的第三个参数不是文件指针，而是文件槽令牌 / 索引。
  - 它会经由 `readQueue + 0xC0` 指向的文件表，把该槽令牌解析成真实条目，再把 `fileEntry + 0x40` 写入低层队列项的 `+0x08`。
  - 这意味着 `sub_14206FEF0` 记录到的 `fileToken` 与 `sub_1427FE940` 看到的 `fileView` 之间需要一次显式映射，不能直接拿来做指针相等比较。
- `Win32ReadQueue_WorkerThread (sub_1427FDB40)` 消费这批低层队列项，并在普通读分支里调用 `Win32ReadQueue_ExecuteRead (sub_1427FE940)`。
- `Win32ReadQueue_ExecuteRead (sub_1427FE940)` 已静态确认会真正落到 `ReadFile`；对当前目标来说，它是目前第一个已经看到“文件对象 + 偏移 + 缓冲 + 长度”同时同场出现的下游点。
- `sub_1420734A0` 仍更像包装层后续线程分支，不应默认视为 `sub_1426C4120` 的直接下一个主线节点。
- `sub_142073210` 当前应单独看待：
  - 静态上它已经明确是 `DecompressingReadDevice` 的请求入口
  - 但运行验证已经表明：当前音乐主链并没有命中这一个包装层入口
- 新一轮围绕活动读设备的最小验证还补充确认：
  - 这轮选中的 `sub_14206A490` 与 `sub_14206FEF0` 虽然都安装成功
  - 但真实的试听、正式播放、下一首流程里依然没有出现任何命中日志
  - 同一轮里 `sub_1426C4120` 的目标 `segment` 记录仍稳定成功
- 因而当前不能把“这轮签名落到的 `sub_14206A490 / sub_14206FEF0`”直接写成已被运行态证实的下一跳。
- `DecompressingReadDevice_SubmitRequest (sub_142073210)` 会直接按 `request + 0x08` 索引包装层分类表，不是简单转发。
- `DecompressingIOThread_MainLoop (sub_142074380)` 会把包装层父请求翻译成新的底层请求：
  - 这份新请求改用数值键、包文件压缩偏移和新的完成回调
  - 因而运行时在 `sub_14206FEF0` 看到数值 `requestKey`，已经有了静态解释
- `DecompressingIOThread_BuildBlockCopies (sub_142073860)` 会把父请求拆成逻辑块描述符，当前已能静态读出：
  - `blockIndex`
  - `inBlockOffset`
  - `dest`
  - `copyBytes`
  - `parentRequest`
- `DecompressingIOThread_FinalizeBlockCopy (sub_1420741A0)` 会在包装层把块数据写回最终输出缓冲，然后才继续父请求完成逻辑。
- 但运行验证已经确认：
  - 当前真实音乐链没有命中 `sub_1420741A0`
  - 所以这条包装层逻辑块覆写线不能再作为当前实现依据

## 下一轮应优先追的方向

- 不再把 `sub_14206FEF0` 的运行态请求当成 `cache:streams/...` 路径对象；这条前提已经被运行日志证伪。
- 继续保留 `sub_1426C4120` 作为上游 `wemResource / wemId` 观测点，并用：
  - `sub_14206FEF0.outHandle == sub_1426C4120.streamHandle`
  - `sub_14206FEF0.callbackCtx == sub_1426C4120.segment`
  做稳定关联。
- 不再单独把 `sub_142073210 / sub_1420741A0` 当成默认主链；运行验证已经表明，不能先假设当前音乐一定走 wrapper。
- 不再把“直接 hook 这轮选中的 `sub_14206A490 / sub_14206FEF0`”当成已证明可观察到真实音乐链的办法；这条前提已经被本轮运行日志否掉。
- 更适合继续推进的候选实现边界，已经从“固定 hook 某个 submit 函数”收束成：
  - 以 `qword_14619D918` 的虚接口为中心
  - 先识别当前活动设备类型
  - 再决定继续走 `NXStorageReadDevice` 低层覆写，还是走 wrapper 分支
- 下一轮应优先确认：
  - 为什么这轮选中的 `sub_14206A490 / sub_14206FEF0` 会在真实音乐流程里完全零命中
  - 这轮签名是否对应到了同职责但并非当前音乐链使用的另一份实现
  - `sub_1426C4120` 在当前运行态之后，到底先交给了哪一个真实提交边界
  - 若真实边界仍落在 `NXStorageReadDevice` 系列，音乐链是否继续沿 `sub_1420708D0 -> sub_1420704E0 -> sub_1427FE5C0 -> sub_1427FE940` 推进
- `requestKey / fileToken / fileView` 的低层映射仍然有价值，但不再是“音乐替换最小闭环”的首要前提。
- 后续运行态关联应优先使用 `wemId / wemResource / streamHandle`，而不是只靠 resolver 时间窗，因为同一资源会在时间窗结束后继续推进更多分块。
