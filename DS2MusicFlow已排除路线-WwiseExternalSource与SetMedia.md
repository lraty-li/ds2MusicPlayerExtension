# DS2 音乐流程已排除路线：Wwise External Source 与 SetMedia

这条路线已被运行日志确认“真实命中但未形成可听替换”。
因此它已经从“待验证”中移出，转为明确排除的旧路线记录。

## 已确认的高层到 Wwise 提交链

- `sub_140C12580` 与 `sub_140C15560` 都会明确调用 `sub_140AC5210`。
- `sub_140AC5210` 明确调用 `sub_142684A30(qword_14A10C580, 0, a1, 0)`。
- `sub_142684A30` 会取资源对象虚表 `+0x20`。
- `WwiseSimpleSoundResource` 与 `WwiseWemSoundResource` 的该槽位都会落到 `sub_1426948C0`。
- `sub_1426948C0` 会构造 `WwiseSimpleSoundInstance`，并把实例挂到 `WwiseSimpleSoundInstance::vftable`。
- `WwiseSimpleSoundInstance` 上至少有以下状态方法会汇到 `sub_142695420`：
  - `sub_142695580`
  - `sub_142695690`
  - `sub_1426956F0`
  - `sub_142695750`
  - `sub_142695990`

## 已确认的 external source 提交点

- `sub_142695420` 会在栈上构造一个单项描述结构，然后调用 `sub_1426B00A0`。
- 从 `sub_142695420` 的汇编可直接读出这项结构的写法：
  - `+0x00` 写入一个 32 位键值，来源于资源相关对象的 `+0xD8`
  - `+0x04` 固定写入 `4`
  - `+0x08` 到 `+0x17` 清零
  - `+0x18` 写入另一个 32 位值，来源于返回对象的 `+0x20`
- `sub_142695420` 传给 `sub_1426B00A0` 的最后两个关键参数是：
  - `a13 = 1`
  - `a14 = &localDescriptor`
- `sub_1426B00A0` 里已明确存在对
  `AK::SoundEngine::PostEvent(uint, uint64, uint, callback, cookie, uint, AkExternalSourceInfo*, uint)`
  的直接调用。
- 因而可以静态确认：
  当前音乐实例不是直接把 `%u.wem` 交给 Wwise，而是会先组一个 1 项 external source 描述，再交给 `PostEvent`。

## 已确认的 SetMedia / UnsetMedia 接口

- 二进制里有真实函数：
  - `AK::SoundEngine::SetMedia`
  - `AK::SoundEngine::TryUnsetMedia`
  - `AK::SoundEngine::UnsetMedia`
- `AK::SoundEngine::SetMedia` 的内部实现是 `sub_1428A4660`。
- `sub_1428A4660` 明确按 `0x18` 步长遍历输入数组，每项至少会读取：
  - `+0x00` 的 32 位键值
  - `+0x08` 的 64 位媒体指针
  - `+0x10` 的 32 位媒体长度
- `sub_1428A4660` 会把这些项挂到一个按 32 位键值索引的内部表上。
- `sub_1428A9D40` 与 `sub_1428AA7C0` 分别支撑 `UnsetMedia / TryUnsetMedia`，它们同样按 `0x18` 步长遍历，并用每项的 `+0x00` 作为查找键。
- 因而可以静态确认：
  Wwise 侧已经具备“按 32 位键值注册一段内存媒体数据”和“按同一键值卸载媒体”的现成接口。

## 基于 IDA 的可行实施方案

- 高层 hook 只负责决定当前播放/试听要替换成哪个外部 `.wem`。
- 真正的数据接入点放在 `sub_142695420` 或 `sub_1426B00A0`，不再依赖 resolver 支线。
- 命中替换条件时：
  - 读取 `sub_142695420` 当前即将提交给 Wwise 的那项 32 位键值
  - 从目录读取外部 `.wem` 全文件到内存
  - 按 `sub_1428A4660` 已证实的 `0x18` 布局构造 1 项 `SetMedia` 输入
  - 调用游戏自带的 `AK::SoundEngine::SetMedia`
  - 再继续走原始 `sub_1426B00A0 -> PostEvent`
- 切歌、停止或替换下一首时：
  - 用同一个 32 位键值调用 `UnsetMedia` 或 `TryUnsetMedia`
  - 再释放我们自己持有的外部 `.wem` 缓冲

## 这条方案为什么比旧线更贴近目标

- 它不要求先证明 `%u.wem`、`cache:streams/...`、`streaming_graph.core` 和播放器当前请求之间的完整运行时闭环。
- 它直接利用了 Wwise 已经存在的“external source 提交”与“内存媒体注册”接口。
- 对“让游戏内播放器播放目录里的额外音乐”这个目标来说，这条路线的核心动作是：
  - 我们自己读目录里的外部 `.wem`
  - 我们自己把字节注册给 Wwise
  - 游戏原本的 `PostEvent` 继续照常触发

## 已通过运行日志确认的关键点

- `sub_142695420` 里参与构造 external source 的那项 32 位键值，已经被运行日志间接确认可用于 `SetMedia / UnsetMedia` 这条绑定尝试链。
- 运行日志已经确认：在当前原型里，`SetMedia` 绑定尝试发生在真实音乐提交链上，而不是只停留在孤立的测试路径。
- 但后续运行样本也确认：这条路线目前还不能等价写成“最终可听替换已经成功”。

## 当前原型实现状态

- 当前工程已经把原型切到这条新路线：
  - 高层仍由 `sub_140C12580 / sub_140C15560` 选择外部 `.wem`
  - `sub_1426B00A0` detour 在 `PostEvent` 前尝试绑定外部 media
  - 绑定动作会先调用游戏自带 `AK::SoundEngine::SetMedia`
  - 绑定成功后，会把传给 `PostEvent` 的 external source 描述改成内存媒体形态
- 本轮运行日志已经确认这套原型会在真实播放链上命中：
  - `externalMusic armed`
  - `sub_142695420 originalExternalSource`
  - `externalMusic wwise bound`
- 本轮实现还新增了按 `WwiseSimpleSoundInstance*` 约束绑定范围的状态控制，因此当前绑定不再只是“armed 就尝试注入”，而是只对当前这次音乐播放实例生效。
- 但结合后续实机样本，这套原型当前只能写成“真实命中并完成绑定尝试”，还不能写成“已经把可听内容替换为目录外部音频”。

## 2026-04-03：`sub_142695420` 签名修正

- 运行时未出现 `sub_142695420 originalExternalSource` 时，先检查了 optional hook 的初始化日志，确认问题不是 detour 逻辑，而是 `sub_142695420` 签名扫描失败。
- 用 IDA 直接读取 `sub_142695420` 函数头机器码后，确认工程中的旧签名有一个字节写错：
  - 工程旧值：`44 8B E9`
  - IDA 实际字节：`41 8B E9`
- 将该字节修正后，再用 IDA `find_bytes` 验证，修正后的签名在 IDB 中唯一命中 `0x142695420`。
- 因而，这一轮得到的结论是：
  - `sub_142695420` 之前扫不到，不是函数本身不稳定，也不是模式长度过长
  - 根因就是签名定义里存在单字节错误
## 2026-04-06：转向 manager 持有的 runtime object 资源句柄

- 本轮结论完全来自 IDA MCP 静态分析，不引用知识文件旧结论。
- `sub_140C12580` 在成功创建播放 runtime 后，把 `sub_140AC5210(soundResource)` 的返回值写到 manager `+6424`。
- `sub_140C15560` 在成功创建试听 runtime 后，把 `sub_140AC5210(trialSoundResource)` 的返回值写到 manager `+10288`。
- `sub_140AC5210 -> sub_142684A30 -> sub_1426948C0` 这一条链明确返回的是 manager 持有的 runtime object 类型。
- `sub_1426948C0` 会把资源句柄写到 runtime object `+0x178`，随后立刻调用 `sub_142680AC0`。
- `sub_140AC5320` 的释放逻辑直接把 runtime object 当成 `a1[47]` 来解，等价于再次证明 `+0x178` 是资源句柄槽位。
- `sub_142680AC0` 和 `sub_142695420` 都会沿着同一条静态路径取资源对象：
  - `runtime + 0x178`
  - `*handle`
  - `*handle + 0x20`
- `sub_142695420` 对这份资源对象至少明确使用了两类字段：
  - `resource + 0xD8` 作为提交给 external source 描述的 32 位键值
  - 调用资源对象虚表 `+0x88`，再读取返回对象 `+0x20`
- `sub_142680AC0` 对这份资源对象还会继续读取多处偏移，当前最值得运行时对照的是：
  - `+0x64`
  - `+0x68`
  - `+0x6C`
  - `+0x75`
  - `+0x80`
  - `+0x84`
  - `+0x88`
  - `+0x98`
  - `+0xBE`
  - `+0xD8`
  - `+0xE0`
- 基于这条静态链，本轮原型不再把重点放在共享的 `sub_142695420` instance 上，而是新增高层探针：
  - 在 `sub_140C12580` 后打印 `currentRuntimeObject` 对应的资源句柄与资源字段
  - 在 `sub_140C15560` 后打印 `trialRuntimeObject` 对应的资源句柄与资源字段
- 这组新增日志仍然只是待验证样本，尚未写入主知识结论。

## 2026-04-06：运行日志确认结果

- 本轮运行日志已经确认，高层新增探针确实命中了 manager 实际持有的 runtime object：
  - `preview trackId=5` 时出现 `sub_140C15560 trialRuntimeResource`
  - `play trackId=57` 与 `play trackId=5` 时出现 `sub_140C12580 currentRuntimeResource`
- 三次样本里，以下指针均发生变化，说明当前观察点已经不是共享的固定 Wwise instance，而是随本次播放/试听动作变化的高层资源链：
  - `runtimeObject`
  - `handle`
  - `handleRoot`
  - `resourceObject`
- 但本轮已打印的资源字段在三次样本里完全一致，尚未呈现歌曲区分度：
  - `res+0x64=0x447A0000`
  - `res+0x68=0x42480000`
  - `res+0x6C=0x3F800000`
  - `res+0x75=0x0`
  - `res+0x80=0x0`
  - `res+0x84=0x0`
  - `res+0x88=0x3F800000`
  - `res+0x98=0x0`
  - `res+0xBE=0x0`
  - `res+0xD8=0xB`
  - `res+0xE0=0xFFFFFFFF`
- 同一批样本里，高层资源链探针始终得到：
  - `sourceInfo=0x0`
  - `sourceInfo+0x20=unreadable`
- 与此同时，`sub_142695420 originalExternalSource` 仍然稳定落在同一个共享实例和同一组键上：
  - `instance=0x44A7971E380`
  - `sourceId=2817842351`
  - `fileId=4187653030`
- 因而，本轮运行日志支持以下修正：
  - `sub_142695420` 这条 external source 线仍然是共享提交点，不是按曲目唯一变化的键
  - 新增的 `runtime + 0x178 -> *handle + 0x20` 探针位置是对的，但当前读取的这批标量字段还不足以识别具体歌曲
- 下一轮真正该扩展观察的对象应是：
  - `resourceObject` 本身的对象头/GUID/虚表相关信息
  - `handleRoot` 关联对象
  - 而不是继续围绕当前这组恒定的 `res+0x64 ... res+0xE0` 标量字段做替换判断

## 2026-04-06：下一轮对象头探针

- 基于本轮日志，下一轮原型新增了更窄的对象头探针，目标不是再扩常量标量字段，而是直接对照三段对象链：
  - 高层 `soundResource / trialSoundResource`
  - `handleRoot`
  - `resourceObject`
- 新探针会额外打印：
  - `soundResource.qwords+0x00`
  - `handleRoot.qwords+0x00`
  - `resourceObject.qwords+0x00`
  - `handleRoot` 与 `resourceObject` 的通用对象头摘要
- 这轮只是把运行前计划写入待验证页，是否真的能提取出可区分歌曲的对象头标识，仍然要以下一轮运行日志为准。

## 2026-04-06：对象头探针运行结果

- 本轮运行日志已经确认，高层 `soundResource / trialSoundResource` 的对象头 `guid` 会随歌曲变化。
- 三次样本里观测到的高层资源 `guid` 分别是：
  - `preview trackId=5`：`56 82 27 78 F3 0E 4A D5 A7 6F CE EE DD 18 B4 9E`
  - `play trackId=57`：`92 7F 62 16 36 3F 47 61 93 68 7E E2 D0 A3 13 A1`
  - `play trackId=5`：`F5 D5 4A 4A 8C 01 44 17 BF DC 6B F7 9C 0E D9 DA`
- 同一批样本里，这份 `guid` 会完整传到 `runtime + 0x178` 解开的两段后续对象链：
  - `handleRoot`
  - `resourceObject`
- 对每次样本来说：
  - `当前命中的高层资源 guid`（试听时为 `trialSoundResource`，正式播放时为 `soundResource`）
  - `runtimeResource.handleRoot guid`
  - `runtimeResource.resourceObject guid`
  三者完全一致。
- 同一首逻辑曲目在试听与正式播放上会落到两份不同的高层资源：
  - `trackId=5` 的 `trackObject guid` 在试听与正式播放样本里都保持 `09 76 3D 99 A2 D1 43 23 BC B5 C2 0D 7D DA 20 35`
  - 试听命中的是 `trialSoundResource guid=56 82 27 78 F3 0E 4A D5 A7 6F CE EE DD 18 B4 9E`
  - 正式播放命中的是 `soundResource guid=F5 D5 4A 4A 8C 01 44 17 BF DC 6B F7 9C 0E D9 DA`
  - 因而当前更合理的理解是：`trackObject` 更像曲目壳对象，其下分别挂着试听资源与正式播放资源
- 日志同时表明：
  - `handleRoot.qwords+0x20` 就是 `resourceObject` 指针
  - `resourceObject.vtbl` 与当前命中的高层资源 `vtbl` 一致
  - `resourceObject` 相比当前命中的高层资源，主要差异是 `u32+0x08` 从 `2` 变为 `3`
- 因而，本轮运行样本更支持这样的暂时理解：
  - `resourceObject` 不是另一首歌的资源，而是同一份高层资源进入运行态后的对象
  - `handleRoot` 是包裹这份资源的上层壳对象
- 与此相对，当前 `externalMusic wwise bound` 里使用的键仍然固定为共享键：
  - `sourceId=2817842351`
  - `fileId=2638440553`
- 所以本轮日志支持把下一轮目标进一步收窄为：
  - 谁消费这 16 字节 `guid`
  - 哪条静态链会用这份 `guid` 去决定真正的播放资源或媒体提交
## 2026-04-06：IDA 静态线索（WwiseWemResource 注册表，待运行确认）

- `sub_1426923B0` 是音频初始化路径的一部分，并且会初始化：
  - `qword_1461C4620 = sub_142692880()`
  - `qword_1461C4628 = sub_142692700()`
- `sub_142693510` 明确会在 `qword_1461C4620` 里按 `request + 0x08` 的 32 位 id 查找对象。
- 命中后，`sub_142693510` 会把查到的对象指针写进结果对象：
  - `result + 0x20 = requestId`
  - `result + 0x30 = WwiseWemResource*`
  - `result + 0x38 = *(resource + 0xC4)`
  - `*(result + 0x00) = *(resource + 0x24)`
- `sub_142693DF0` / `sub_142693F30` 可以确认 `WwiseWemResource` 是独立类，不是简单的 fileId 壳对象；其对象内显式包含：
  - `+0x20` 的 32 位 id
  - `+0x70` / `+0x88` 一带的 `MemoryStream / IReadStream`
  - `+0xC4`
  - `+0xF4`
- `sub_14028DE70` 与 `sub_1426AD330` 会在创建完 `WwiseWemResource` 后填充关键字段并调用 `sub_1426940C0` 注册到 `qword_1461C4620`：
  - `resource + 0x10` <- 父对象 guid
  - `resource + 0x20` <- 一组 32 位 id 数组中的当前项
  - `resource + 0x24` <- 表项中的 32 位值
  - `resource + 0xC0 .. +0xD7` <- 表项复制过来的 24 字节块
  - `resource + 0xEC` <- 一组 32 位数组中的当前项
  - `resource + 0xF4` <- 当前索引
- 由此，当前新的待验证主线是：
  - 播放器动作命中高层入口
  - 某处最终触发 `sub_142693510`
  - `sub_142693510` 取回真实 `WwiseWemResource`
  - 若要替换外部音乐，候选方案不再是 shared external source，而是：
    - 直接在 `sub_142693510` 返回阶段替换 `WwiseWemResource*`
    - 或者预先构造并注册我们自己的 `WwiseWemResource`
- 当前这些都还是 IDA 静态线索，尚未通过运行日志确认它们就是播放器这条链上的实际命中点。

## 2026-04-06：可替换点继续收窄（高层资源 -> 当前 WwiseWemResource）

- `sub_142695420` 已可静态确认是实际播放提交链上的关键点：
  - 它先从 `runtime + 0x178` 取出资源句柄
  - 再解到 `handleRoot.qwords+0x20 -> resourceObject`
  - 然后对 `resourceObject` 调用虚表 `+0x88`
- 这次 `+0x88` 虚调用的返回值，不是抽象壳对象，而是“当前可播的 `WwiseWemResource*`”：
  - 随后 `sub_142695420` 直接读取返回对象 `+0x20`
  - 同时读取 `resourceObject + 0xD8`
  - 用这两项拼出单项 `AkExternalSourceInfo`
  - 最终交给 `sub_1426B00A0 -> AK::SoundEngine::PostEvent`
- 这意味着：从“高层资源对象”到“当前真正可播资源”的解引用，已经不再是猜测，而是明确发生在 `resourceObject` 的虚表 `+0x88` 上。

- 对 `LocalizedSimpleSoundResource` 这条分支，虚表 `+0x88` 落到 `LocalizedSimpleSoundResource_GetCurrentWemResource`：
  - 它会先按当前语言/变体调用 `LocalizedSimpleSoundResource_SelectVariantAndRegisterWem`
  - 如果当前语言没有命中，再回退到 `1`
  - 最终返回对象内缓存的当前 `WwiseWemResource*`

- 对 `WwiseWemLocalizedResource` 这条分支，当前不能再直接把 `0x1426ADA10` 写成“虚表 `+0x88` 对应的 getter”：
  - 已确认 `0x1426ADA10` 的直接调用者是 `sub_140671AA0` 与 `sub_140685840`
  - 它会遍历全局注册的本地化资源对象，并批量调用 `WwiseWemLocalizedResource_SelectVariantAndRegisterWem`
  - 因而它当前更像“全局本地化刷新入口”，而不是“返回单个当前 `WwiseWemResource*` 的直接 getter”
  - `sub_1426ACDB0` 内部也会对单个对象调用 `WwiseWemLocalizedResource_SelectVariantAndRegisterWem`，但当前仍不足以静态确认真正被 `resourceObject.vtbl+0x88` 直接命中的落点
- 因而当前能静态确认的只有：
  - `WwiseWemLocalizedResource_SelectVariantAndRegisterWem` 会维护对象内缓存的当前 `WwiseWemResource*`
  - 但 `WwiseWemLocalizedResource` 分支上真正对应 `resourceObject.vtbl+0x88` 的函数，仍待下一轮运行日志与 detour 命中顺序确认

- `LocalizedSimpleSoundResource_SelectVariantAndRegisterWem` 与 `WwiseWemLocalizedResource_SelectVariantAndRegisterWem` 的共同点已经很明确：
  - 复用或新建“当前 `WwiseWemResource` 子对象”
  - 把父资源 `+0x10..0x1F` 的 16 字节 `guid` 原样复制到子对象 `+0x10..0x1F`
  - 填充子对象 `+0x20` 的 32 位 id 与其余元数据
  - 调用 `RegisterWwiseWemResourceById` 挂到全局表 `qword_1461C4620`

- `ResolveWwiseWemResourceByRequestId` 则是更下游的单点查找器：
  - 按 `request + 0x08` 的 32 位 id 到 `qword_1461C4620` 里查找
  - 命中后，把 `WwiseWemResource*` 写到结果对象 `+0x30`
  - 因为这个子对象此前已经复制了父资源 `guid`，所以在这里仍然能回溯到上游 `per-track guid`

- 基于以上静态链，当时曾把以下两处当作候选替换点：
  - 高层替换点：拦截 `resourceObject` 虚表 `+0x88` 对应的“获取当前 `WwiseWemResource`”路径，直接把返回对象换成我们的外部资源
  - 下游替换点：拦截 `ResolveWwiseWemResourceByRequestId`，在它写回结果 `+0x30` 前后把命中的 `WwiseWemResource*` 替换掉

- 这两个点相比“去找原始音频存放位置”更符合目标：
  - 它们都已经位于真实播放链上
  - 都直接面向 `WwiseWemResource*` 这种可播资源对象
  - 都可以利用已确认的 `per-track guid` 判断“当前是哪首曲目”，再映射到目录里的外部音乐文件

- 上面这组判断只保留为当时的静态收窄背景，不能再视为当前主线结论。

## 2026-04-06：运行日志确认 SetMedia 路线已命中真实播放链

- 本轮运行日志里，`sub_142695420` 与 `sub_1426B00A0` 的 hook 已明确安装成功：
  - `MH_CreateHook=MH_OK`
  - `MH_QueueEnableHook=MH_OK`
  - `MH_ApplyQueued=MH_OK`
- 试听样本 `preview=true trackId=5` 中，日志顺序是：
  - `externalMusic armed ... path=...preview.wem`
  - `sub_142695420 originalExternalSource ...`
  - `externalMusic wwise bound ... path=...preview.wem`
- 正式播放样本 `preview=false trackId=57` 与 `preview=false trackId=5` 中，日志顺序同样成立：
  - `externalMusic armed ... path=...play.wem`
  - `sub_142695420 originalExternalSource ...`
  - `externalMusic wwise bound ... path=...play.wem`
- 因而，本轮运行日志已经确认：
  - 目录中的 `preview.wem / play.wem` 已经被绑定进真实音乐事件
  - 当前最小可行替换链是 `高层选曲 -> SetMedia 绑定 -> 原始 PostEvent 提交`
- 同一轮样本里，`sub_142695420` 反复看到的仍是共享 external source 槽：
  - `instance=0x57171742D80`
  - `sourceId=2817842351`
  - `fileId=4187653030`
- 这进一步说明：
  - 这组 `sourceId/fileId` 仍然不是 per-track 标识
  - 但在高层曲目已经选定之后，它已经足够作为目录外部音频接入的共享注入槽
- 本轮日志还确认了当前原型状态控制是有效的：
  - 切歌前会出现 `externalMusic wwise unset reason=track-change`
  - 同一轮里会多次命中 `sub_142695420`，但只产生一次有效 `externalMusic wwise bound`
- `sub_142693510` 仍然会在后续照常出现并携带变化的 `requestFileId`，但当前替换链已经在它之前完成。
- 因而，本页对应的这条方案已经从“仅靠静态分析的候选路线”前进到“已被运行日志确认命中的真实注入路线”。

## 2026-04-06：运行日志修正（命中并绑定，不等于可听替换）

- 本轮运行日志再次确认，当前原型会稳定出现：
  - `externalMusic armed`
  - `sub_142695420 originalExternalSource`
  - `externalMusic wwise bound`
- 同时，路径本身也被日志直接确认没有问题：
  - `preview.wem / play.wem` 都被成功选中
  - 都打印了有效 `fileSize`
  - 因而当前问题不是路径错误，也不是文件打开失败
- 但同一轮实机结果是“未感知到音频替换”，因此必须修正本页前面的阶段性结论：
  - `externalMusic wwise bound` 只能证明 `SetMedia` 绑定尝试成功发生
  - 不能证明最终可听音乐已经来自这些外部 `.wem`
- 这轮里更关键的负信号是：
  - 试听第一次 `wwise bound` 较晚，可能已经错过试听窗口
  - 但正式播放样本中的 `wwise bound` 几乎紧跟在 `sub_142695420` 之后发生，仍然没有形成可听替换
- 因而，这轮更支持以下修正判断：
  - shared external source 提交点虽然是真实提交链上的注入点
  - 但它大概率还不是最终决定可听内容的替换点
  - 或者当前改写的 external source / memory media 形态没有被最终发声路径真正采纳
- 本轮还确认了新增的 `slotIndex` 已经进入高层选文件日志，但实际命中的仍然是 `preview.wem / play.wem`，说明“按位置映射目录文件”的后备逻辑这轮没有参与生效。
- 因而，`SetMedia` 路线已经可以明确判定为错误方向：
  - 它虽然真实命中
  - 也证明路径正确、文件可读
  - 但没有形成最终可听替换
  - 后续不准再沿这条路线推进

## 2026-04-06：后续实机反馈修正（LocalizedSimpleSoundResource 方向同样停止）

- 根据后续实机反馈，沿 `resourceObject.vtbl+0x88 -> LocalizedSimpleSoundResource_GetCurrentWemResource` 这条路线继续尝试，也没有形成可听替换。
- 因而这里必须把这条线写成明确结论，而不是“高层候选点”：
  - `LocalizedSimpleSoundResource_GetCurrentWemResource` 仍可保留为静态资源解析链上的真实节点
  - 但它不是可行替换点
  - 后续不准再沿这条路线推进
- 结合前面的 `SetMedia` 结果，本页当前已经明确排除的方向是：
  - shared external source / `SetMedia`
  - `resourceObject.vtbl+0x88 -> LocalizedSimpleSoundResource_GetCurrentWemResource`
- 因而后续主线应固定为：
  - 继续沿 `guid -> WwiseWemResource* -> 最终发声提交` 往更下游找真实消费点
  - 不再回到这两个已经明确排除的方向
