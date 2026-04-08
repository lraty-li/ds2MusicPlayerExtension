# DS2 音乐流程待验证记录：文件定位与读链

本页只保留“resolver 已命中之后，真实可听数据链到底落到哪里”的开放问题。
已经被运行日志确认的结论已移入主知识文件与流程总览，不再在本页重复展开。

## 当前仍需闭环的问题

- `sub_142693510 -> sub_1426C4120` 已经通过运行日志稳定闭环，但其后的真实提交节点仍未最终钉死。
- `sub_1426C4120` 这轮已经运行命中补齐 `submitCtx / dstBuffer / readBytes / logicalOffset`，因此当前最需要确认的是：`off_14407FB20` 运行时对象的虚表槽位 `[4]` 最终指向谁，以及它返回的异步句柄是如何完成与回收的。
- preview 与正式播放已确认都会命中 `sub_1426C4120`，但它们在 `off_14407FB20[4]` 之后是否还会再次分叉，仍需确认。
- `sub_1426C4120` 里当前记录到的 `dstBuffer` 是否就是后续真实消费的目标缓冲，而不是中间 staging buffer，仍需运行时确认。
- 当前没有任何 `dynamic submitter.slot4` 安装日志或 `submitter.slot4` 命中日志；这是“运行时槽位 `[4]` 未被成功观测到”，还是“真实消费边界不在该函数本体”，仍需确认。

## 需要继续依赖 IDA 下钻的对象问题

- `off_14407FB20` 运行时对象的虚表槽位 `[4]` 在启动后是否会被改写到静态命名之外的真实实现。
- `sub_1426C4120` 传给 `off_14407FB20[4]` 的第二实参 `resource + 0x10`、以及塞进 transfer 结构的 `resource + 0xC0 / +0xD0`，分别代表哪些提交侧元数据。
- `sub_1426C4120` 里 `segmentDesc + 0x10` 这块缓冲，后续是被谁消费、是否允许同步覆写后直接视作本段已完成。
- 若仍想继续保留低层 `Win32ReadQueue_ExecuteRead` 作为备选，则必须继续把 `wemResource / streamHandle / segment` 映射到 `fileView / segmentEntry`。

## 已获得的证据

### 运行已命中

- 在目标操作流程里，`sub_1426C4120` 这轮已经直接记录到：
  - `submitCtx`
  - `streamObject`
  - `wemResource`
  - `wemId`
  - `logicalOffset`
  - `readBytes`
  - `dstBuffer`
- 上述字段已在以下资源样本里复现：
  - 试听第二首：`wemId=647888142`
  - 正式播放第一首：`wemId=609479110`
  - 正式播放第二首：`wemId=780084104`
  - 再次正式播放第一首：`wemId=609479110`
- 同一资源的前两段推进已稳定表现为：
  - `logicalOffset=0`
  - `logicalOffset=131072`
  - `readBytes=131072`
- 因而 `sub_1426C4120` 当前已经不是单纯静态候选，而是运行已命中的资源级提交边界。

### 静态候选

- `sub_1426C4120` 不是直接提交读请求；它会调用 `off_14407FB20` 的虚表 `+0x20`，当前落点是 `sub_14206A490`。
- `off_14407FB20` 默认先指向静态 submitter 对象 `0x14407F9F8`，其首字段就是 submitter vtable `0x1433C9CE0`。
- `InitStreamCacheSubmitterThread (sub_1426E4670)` 会把 `off_14407FB20` 改成运行时对象 `a1 + 0x20`，但仍复用同一套 submitter 虚接口。
- `sub_1426C4120` 当前已静态补全出一组更直接的提交侧字段：
  - `streamObject + 0x30 -> WwiseWemResource*`
  - `streamObject + 0x38 -> segment 基偏移`
  - `segmentDesc + 0x00 -> 当前段相对偏移`
  - `segmentDesc + 0x0C -> 当前段长度`
  - `segmentDesc + 0x10 -> 当前段输出缓冲`
- 结合上面的运行命中样本，当前可以把它写成：
  - 当前第一个已经运行命中并补齐 `wemResource / submitCtx / dstBuffer / readBytes / logicalOffset` 的边界
- `sub_1426C4120` 还会把：
  - `resource + 0x10`
  - `resource + 0xC0`
  - `resource + 0xD0`
  拆进提交给 `off_14407FB20[4]` 的参数与 transfer 结构。
- 这意味着当前更小的静态替换边界，已经从“伪造完整 `WwiseWemResource`”收缩成：
  - `sub_1426C4120`
  - 或其调用的运行时 `off_14407FB20` 槽位 `[4]`
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
- 但低层队列链这轮也补上了一条新的静态限制：
  - `sub_14206FEF0` 返回的 handle 只能可靠映射回 `opContext`
  - 到 `sub_1420704E0` 后就会被摘成“文件槽位 + 偏移 + 缓冲 + 长度”并可能合并
  - `sub_1427FE5C0 / sub_1427FE940` 里已经看不到 `request handle / callbackCtx / wemResource / streamHandle`
- 因而 `Win32ReadQueue_ExecuteRead` 虽然机械上具备：
  - 输出缓冲
  - 读取长度
  - 逻辑偏移
  但当前仍缺“目标资源 -> fileView / segmentEntry”的稳定关联桥，不能直接当作理论上完全可行的最终边界。
- `sub_1420734A0` 仍更像包装层后续线程分支，不应默认视为 `sub_1426C4120` 的直接下一个主线节点。
- `sub_142073210` 当前应单独看待：
  - 静态上它已经明确是 `DecompressingReadDevice` 的请求入口
  - 但运行验证已经表明：当前音乐主链并没有命中这一个包装层入口
- 包装层最终写回 `sub_1420741A0` 这轮也被进一步降级：
  - 它静态上确实拿得到 `dst / len / blockIndex + inBlockOffset`
  - 但稳定键已经退化成 `(packageId, chunkIndex)` 级别
  - 这更像共享包块缓存写回点，而不是适合作主替换边界的资源级节点
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

## 已获得的运行时反证

- 即使把 submit route hook 的安装时机前移到 resolver / `sub_1426C4120` 之前，当前选中的 `sub_14206A490 / sub_14206FEF0` 在真实试听、正式播放、下一首流程里仍然零命中。
- 即使直接 hook `KernelBase!ReadFile`，同一套真实音乐流程里也没有任何命中日志。
- 同时 `sub_1426C4120` 的目标 `segment` 记录仍然稳定成功。
- `sub_142692E90` 与 `sub_142692EE0` 这轮也已明确形成反证：
  - hook 安装成功
  - 真实试听、正式播放、下一首流程里真正命中数仍然为 `0`
  - 因而当前不能继续把它们表述成音乐分段请求的实际完成 / 释放主路径
- 因而当前更合理的解释不再是“第一次提交发生在 hook 安装前”，而是：
  - 运行时真实 submit 落点不是这轮直接 hook 到的静态函数本体
  - 或者 `sub_1426C4120` 之后的真实消费边界比当前探测到的 submit / `ReadFile` 更靠后

## 下一轮应优先追的方向

- 不再把 `sub_14206FEF0` 的运行态请求当成 `cache:streams/...` 路径对象；这条前提已经被运行日志证伪。
- 继续保留 `sub_1426C4120` 作为当前唯一主替换边界假设的上游观测点。
- 不再把低层 `Win32ReadQueue_ExecuteRead` 当成“已经补齐条件的最终实现边界”；它目前仍缺资源关联桥。
- 不再把 `sub_142073210 / sub_1420741A0` 当成默认主链；运行验证已经表明，不能先假设当前音乐一定走 wrapper。
- 不再把“直接 hook 这轮选中的 `sub_14206A490 / sub_14206FEF0`”当成已证明可观察到真实音乐链的办法；这条前提已经被运行日志否掉。
- 不再把 `sub_142692E90 / sub_142692EE0` 当成默认完成路径；这条前提也已经被本轮运行日志否掉。
- 下一轮唯一优先确认的运行时边界，收缩为：
  - `off_14407FB20` 运行时对象的槽位 `[4]`
  - 记录对象地址、vftable 地址、槽位真实函数指针
  - 解释为什么当前没有任何 `dynamic submitter.slot4` 安装日志或命中日志
  - 记录返回句柄与真实完成路径，确认能否在该边界把外部文件同步灌入 `dstBuffer`
- 后续运行态关联应优先使用 `wemId / wemResource / streamHandle / segment`，而不是只靠 resolver 时间窗，因为同一资源会在时间窗结束后继续推进更多分块。
