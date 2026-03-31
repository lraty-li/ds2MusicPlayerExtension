# DS2 音乐流程待验证记录（IDA 静态下钻）

本文件只记录本轮通过 IDA MCP 得到、但尚未经过运行时验证的结论。

- 这些内容不能直接视为知识库主结论。
- 只有在后续运行时日志、hook 观测或重复静态证据进一步支撑后，才应迁入主知识文件。

## 当前待验证点

### `DSMusicPlayerTrackResource`

- IDA 静态类型里，`DSMusicPlayerTrackResource` 看起来包含：
  - `trackId = +0x20`
  - `soundResource = +0x40`
  - `trialSoundResource = +0x48`
- 这与当前运行时日志里对 `entry` 的解读一致，但仍应继续用运行时证据交叉验证。

### `sub_140AC5210`

- `sub_140AC5210` 在 IDA 里看起来只是一个较薄的包装层。
- 它内部直接调用：
  - `sub_142684A30(qword_14A10C580, 0, a1, 0)`
- 成功后还会补做 `_on_start_` 一类启动事件调用。

### `sub_142684A30`

- 目前看它更像 `sub_140AC5210` 下层的原生资源桥接点。
- 它不会自己直接按文件或曲目表做解析，而是先对传入资源对象调用其虚函数 `+0x20`。
- 当前 IDA 样本里已看到：
  - `WwiseSimpleSoundResource::vftable + 0x20 -> sub_1426948C0`
  - `WwiseWemSoundResource::vftable + 0x20 -> sub_1426948C0`
  - `SimpleSoundResource::vftable + 0x20 -> sub_14280BC80`

### `StreamingManager / ObjectStreamingSystem`

- `qword_1461C4638` 在 IDA 里看起来是 `StreamingManager` 全局实例。
- 它的初始化路径当前看到的是：
  - `sub_140691BF0 -> sub_1426C7100`
- `sub_1426C7100` 会分配并初始化 `ObjectStreamingSystem`，随后在其 `+0xE0` 附近内嵌构造 `ObjectStreamingReader`：
  - `ObjectStreamingReader` 构造函数：`sub_1426C6B10`
  - `ObjectStreamingSystem` 的工作线程启动：`sub_1426E4670`
- `sub_1426948C0` 与 `sub_14280BC80` 这两条路径，看起来都会进一步汇合到：
  - `StreamingManager::vftable + 0x20 -> sub_1426C8000`
- `sub_1426C8000 -> sub_1426C7E20 / sub_1426C7A90` 当前看起来会把资源对象 `+0x10/+0x18` 的 16 字节值作为 key 做分桶、查找与缓存项创建。
- `SoundResource` 构造函数 `sub_1426842C0` 也会把 `+0x10/+0x18` 初始化为 0。
- 当引用计数首次建立时，还会进一步命中 `ObjectStreamingSystem::sub_1426E54A0`，继续按同一组 key 做登记。
- `sub_1426C8270` 当前更像“GUID 绑定后的通知/分发层”，不像实际解码或读文件层：
  - 它会在 `a1 + 176` 的表里按当前 handle 查回调项
  - 若状态位满足条件，则对查到的对象调用其虚函数 `+0x8`
  - 传入参数包括 `*(_QWORD *)(*a2 + 32)` 与表项上下文
  - 因此它更像 waiter / event / listener 分发，而不是“把资源变成可读音频数据”的函数
- 当前样本里已对上 `ObjectStreamingSystem` 的一组关键虚表槽位：
  - `+0x50 -> sub_1426E4F40`
  - `+0x60 -> sub_1426E50D0`
  - `+0x68 -> sub_1426E54A0`
  - `+0x78 -> sub_1421F3080`
  - `+0x80 -> sub_1426E5680`

### `ObjectStreamingReader`

- `ObjectStreamingReader` 当前看起来不是单纯的 GUID 绑定器，而是 `ObjectStreamingSystem` 下游的“对象流解析器 + 读缓冲生产者”。
- 它至少包含两组职责：
  - 启动阶段读取并整理 `streaming graph / group data`
  - 运行阶段把对象流记录继续推进成上层可消费的读缓冲
- 当前静态下钻里，处理 `ObjectStreamingTable` 压缩布局的关键函数组是：
  - `sub_1426E47A0`
  - `sub_1426E6CB0`
  - `sub_1426E6900`
  - `sub_1426E90A0`
  - `sub_1426E9690`
- 上述三条函数 `sub_1426E6900 / sub_1426E90A0 / sub_1426E9690` 已直接引用报错字符串：
  - `Unknown ObjectStreamingTable compression type %d`
- 这说明 `ObjectStreamingReader` 已经触到真实的对象流表与压缩布局，而不只是 GUID 哈希表。
- 当前运行态调度链更像是：
  - `sub_1426E5E60`
  - `sub_1426E5D10`
  - `sub_1426E6020`
  - `sub_1426E6F70`
- 其中：
  - `sub_1426E5E60` 是 `StreamingThread` 的主循环壳子
  - `sub_1426E5D10` 是每次线程唤醒后的批处理调度点
  - `sub_1426E6020` 是更靠近实际对象流批处理的核心入口
  - `sub_1426E6F70` 会把当前对象流请求继续分发给已登记的 reader / source
- `ObjectStreamingReader` 自身虚表附近当前看到的更关键运行态方法包括：
  - `sub_1426E7E90`：更像对象流记录与引用的变长解析层
  - `sub_1426E7C00`：更像 `MsgReadBinaryStream` 包装层
  - `sub_1426E7CC0`：当前更像“按 streamDesc / 模板材料化对象实例”的函数
- 本轮静态复核后，对 `sub_1426E7CC0` 的理解已修正为：
  - 它仍然会检查底层 `streamDesc` 形态，并据此选择回调、跨度和元素宽度
  - 但对音乐相关模板 `word_145FB7580 / unk_145FDB1B0` 而言，`sub_140103520` 选出的回调分别落到：
    - `sub_142695340 / sub_142695390`：`WwiseSimpleSoundInstance` 的构造/重置
    - `sub_14280A6D0 / sub_14280A720`：`SimpleSoundInstance` 的构造/重置
  - 因此这层当前更像对象流材料化层，而不是“最终可读音频字节”产出层
  - 上一轮把它视作最接近音频数据输出点的判断，已被本轮静态证据削弱

### 当前收束出来的候选原生解析链

- 目前更像是：
  - `sub_140C12580 / sub_140C15560`
  - `sub_140AC5210`
  - `sub_142684A30`
  - `[资源对象 vfunc +0x20]`
  - `StreamingManager::sub_1426C8000`
  - `sub_1426C7E20 / sub_1426C7A90`
  - `ObjectStreamingSystem::sub_1426E54A0`
- 如果继续往“GUID 绑定之后，谁真正去读数据”收束，当前更像会落到：
  - `ObjectStreamingSystem` 工作线程：`sub_1426E5E60 -> sub_1426E5D10`
  - 批处理入口：`sub_1426E6020`
  - reader 分发：`sub_1426E6F70`
  - reader 运行态方法：`sub_1426E7E90 / sub_1426E7C00 / sub_1426E7CC0`
- 这条链还不能直接写入主知识库，后续需要继续验证：
  - `+0x10/+0x18` 是否真的就是运行时 GUID
  - `sub_1426E7CC0` 对音乐资源是否始终只负责对象实例材料化，而不会直接产出媒体字节
  - 音频资源最终落到的 `ObjectStreamingReader` 分支，是否与纹理/模型等对象流共用同一套 reader 运行时

### 本轮新增：`WwiseSimpleSoundInstance` 到 Wwise 事件层

- 对 `WwiseSimpleSoundResource::vftable + 0x20 -> sub_1426948C0` 的继续下钻表明：
  - `sub_1426948C0` 会构造 `WwiseSimpleSoundInstance`
  - 期间仍会通过 `StreamingManager::vftable + 0x20 -> sub_1426C8000` 绑定资源
  - 但实例构造完成后，还会再按模板 `word_145FB7580` 做一次对象材料化
- 与该模板直接相连的一组 Wwise 相关对象当前已浮出：
  - `WwiseBankResource`
  - `SoundSourceVoiceTemplate`
  - `SoundMasterVoiceTemplate`
- `WwiseBankResource` 析构 `sub_142695D10` 已明确调用：
  - `AK::SoundEngine::UnloadBank(...)`
- 这说明当前音乐链至少有一层已经进入 Wwise bank / voice 资源管理，而不再只是通用对象流层
- 对 `WwiseSimpleSoundInstance` 虚表方法的静态下钻显示：
  - `sub_1426956F0 / sub_142695750 / sub_142695860 / sub_142695990` 等状态推进函数最终都会汇到 `sub_142695420`
  - `sub_142695780 / sub_142695860` 已明确调用 `AK::SoundEngine::ExecuteActionOnEvent(...)`
  - `sub_142695580` 还会调用 `AK::SoundEngine::SetRTPCValue(...)` 与 `AK::SoundEngine::SeekOnEvent(...)`
- 当前静态上更像的 Wwise 音乐发起链是：
  - `sub_140C12580 / sub_140C15560`
  - `sub_140AC5210`
  - `sub_142684A30`
  - `sub_1426948C0`
  - `sub_142695420`
  - `sub_1426B00A0`
  - `AK::SoundEngine::PostEvent(...)`

### 本轮新增：`sub_1426B00A0` 与 external source 形态

- `sub_1426B00A0` 不是通用包装壳，已静态确认会直接调用：
  - `AK::SoundEngine::PostEvent(uint, uint64, uint, callback, cookie, uint, AkExternalSourceInfo *, uint)`
- 其回调参数固定落到：
  - `sub_1426AF230`
- `sub_1426AF230` 内已直接出现：
  - `MusicEnd`
  - `MusicTelop`
- 从 `sub_142695420` 与 `sub_1426B00A0` 的汇编配合看：
  - `sub_142695420` 会在栈上组一个 1 项的小结构，再把它作为 external source 参数传入 `PostEvent`
  - 当前形态更像“按 fileId 描述外部源”，而不是“直接按路径传字符串”
  - 该小结构里至少可稳定看出的字段包含：
    - 一个来自资源/voice 配置的 `sourceId`
    - 常量 `4`
    - 一个来自资源对象 `+0x20` 的 `u32`
    - 其余若干指针/长度位当前为 0
- 这组字段当前最像 `AkExternalSourceInfo` 的一份精简描述，但具体每个字段含义仍需运行时或类型信息验证

### 本轮新增：对 `StreamingProgramResource / GraphProgramStreamingStrategy*` 的降级判断

- 本轮继续下钻 `StreamingProgramResource / GraphProgramStreamingStrategyResource / GraphProgramStreamingStrategyInstance / DSAreaBased* / DSTileBased*` 后，当前看到的主要仍是：
  - 构造
  - 析构/清理
  - `LevelData / LevelGroup / LevelSymbols` 一类流式资源容器
- 这批对象目前更像通用关卡/对象流策略层，尚未出现足以把它们直接判为“最终音乐媒体读取点”的证据
- 因而它们在“额外音乐重定向候选”里的优先级，当前低于 `WwiseSimpleSoundInstance -> sub_142695420 -> sub_1426B00A0`

### 当前更可能的原生重定向切入层

- 本节只代表当时基于静态证据的优先级判断；后续运行时已显示 `sub_142695420` 更像播放器相关常驻状态机，因此它不再是当前主线实现目标。
- 仅从这批静态证据看，对“原生外部文件重定向切入层”的判断当时是：
  - 更可能靠近 `WwiseSimpleSoundInstance` 的 Wwise 事件发起层
  - 而不是 `ObjectStreamingReader` 的对象流材料化层
- 当前最可疑的候选切入点顺序更像是：
  - `sub_142695420`
  - `sub_1426B00A0`
  - Wwise 更下层的 file location / file open resolver
- 选择这层的原因是：
  - `sub_142695420` 已处在音乐实例专用链路上
  - 它会组 external source 描述并直接交给 `AK::SoundEngine::PostEvent`
  - 如果要接入目录里的额外 WEM，更像应在这里改写 external source 描述，或继续往 Wwise 的 fileId 解析层下钻
- 相比之下：
  - `sub_1426C7E20 / sub_1426C8270 / sub_1426E54A0` 仍更像索引、登记、通知与队列层
  - `sub_1426E7CC0` 当前更像对象实例材料化层，已不再是首选重定向边界
- 后续若要继续落地“额外音乐接入”，当前运行时主优先级应转向主知识文件里已确认会执行的 `%u.wem` / `sub_1426932B0` 这条线；本节保留仅作静态历史判断。

### 本轮新增：`"%u.wem"` 字符串引用链

- 当前 IDA 字符串表里，`"%u.wem"` 只定位到一个宽字符串常量：
  - `0x143438C30 -> L"%u.wem"`
- 该字符串目前只看到两处直接引用：
  - `sub_1426932B0`
  - `sub_1426C3B10`
- 这说明 `"%u.wem"` 不是播放器 UI 层文本，而是更靠近底层音频资源命名/解析链的固定格式串。

### 本轮新增：`sub_1426C3B10` 更像 WEM/BNK 名称拼装层

- `sub_1426C3B10(a1, a2, a3, a4)` 当前更像一个“根据资源描述拼装最终文件名/相对路径”的辅助函数。
- 它会根据传入对象状态在两种格式串之间切换：
  - `L"%u.bnk"`
  - `L"%u.wem"`
- 当资源对象里已经带有显式路径指针时，它优先直接使用该路径。
- 否则它会取资源里的 `u32` 标识，调用 `swprintf` 生成 `%u.wem` 或 `%u.bnk` 文件名，再把结果拼接到：
  - 调用方传入的基路径
  - 或 `a1 + 8` 指向的默认基路径
- 当前看它更像“名称/路径构造器”，而不像最终 OS 文件打开点。
- 它当前只看到被 `sub_142692F60` 调用。

### 本轮新增：`sub_142692F60` 更像路径描述/调试文本拼装层

- `sub_142692F60` 会遍历包列表并输出类似：
  - `Package #%u; `
- 它随后调用 `sub_1426C3B10(...)` 把每个条目的 `.wem/.bnk` 名称或相对路径继续拼到输出缓冲。
- 当前更像“生成可读路径描述/调试文本”的函数，而不是实际加载器本体。

### 本轮新增：`sub_1426932B0` 更像按 fileId/name 解析 WEM 的候选加载层

- `sub_1426932B0` 同样直接引用 `L"%u.wem"`，但职责明显比 `sub_1426C3B10` 更重。
- 当前静态下钻看到它会：
  - 在资源对象未自带显式名称时，用 `*(u32 *)(a2 + 8)` 格式化出 `%u.wem`
  - 进入 `a1 + 72` 的临界区
  - 遍历 `a1 + 64` 上的链表/容器项
  - 按名称/哈希/附加参数调用：
    - `sub_1426C43D0`
    - `sub_142693BD0`
    - `sub_1426C4310`
    - `sub_1426C46E0`
  - 成功分支里还会通过 `(*(*a1) + 16)` 一类虚调用创建并回填结果对象
- 从控制流形态看，它已经不像单纯字符串辅助，而更像：
  - 按 `fileId -> "%u.wem"` 名称
  - 在某个包/目录索引里查找
  - 构造一个后续可读/可消费的结果对象
- 它是否就是“最终文件打开函数”还不能直接下结论，但当前比 `sub_1426C3B10` 更接近真实加载层。

### 本轮新增：对 `"%u.wem"` 线索的阶段性判断

- 仅靠 `"%u.wem"`，当前已经能把链路分成两层：
  - 名称/路径拼装：`sub_1426C3B10`
  - 更接近包内 WEM 解析/结果对象构造：`sub_1426932B0`
- 这条链当前对“额外音乐加载”是有价值的，因为它提示：
  - 游戏底层很可能并不是把完整路径一路上传
  - 而是经由 `fileId -> "%u.wem"` 命名规则，再进入包/索引解析层
- 因而后续若继续找“最适合外部 WEM 重定向”的候选点，`sub_1426932B0` 及其下游辅助函数当前值得优先继续下钻：
  - `sub_142693BD0`
  - `sub_1426C46E0`
  - `sub_1426C43D0`
  - `sub_1426C4310`

### 本轮新增：`sub_142693BD0 / sub_1426C46E0` 的职责细化

- 对 `sub_1426932B0` 下游两条关键分支的继续下钻显示：
  - `sub_142693BD0` 更像“按名称哈希 + 变体号，在排序表里二分查找并创建结果对象”的分支
  - `sub_1426C46E0` 更像“按 fileId + 变体号，在另一张排序表里二分查找条目”的纯查表辅助
- `sub_142693BD0(a1, a2, a3, a4, a5)` 当前静态上可稳定看出的形态：
  - 进入 `a1 + 72` 临界区
  - 要求 `*(_DWORD *)a4 == 1`
  - 取 `a2 + 56` 指向的表，按 `a3` 与可选变体值做二分查找
  - 命中后通过 `(*(*a1) + 16)` 创建结果对象并回填
  - 返回值形态与 `sub_1426932B0` 一致，当前已看到：
    - `1`：成功
    - `52`：对象分配失败
    - `66`：未命中/条件不满足
- `sub_1426C46E0(a1, fileId, table, useVariant)` 当前更像纯查表函数：
  - 若启用变体，则取 `*(u16 *)(a1 + 8)` 作为附加匹配键
  - 对 `table` 做二分查找
  - 返回命中的表项指针，失败返回 `0`
- 这说明 `sub_1426932B0` 已经不只是“格式化 `%u.wem` 名称”，而是会在：
  - 名称哈希查找分支：`sub_142693BD0`
  - fileId 查找分支：`sub_1426C46E0`
  之间切换，再统一构造结果对象
- 因而当前对 hook 价值的优先级判断进一步收束为：
  - 主候选：`sub_1426932B0`
  - 辅助观察：`sub_1426C3B10`
  - 更深层查表辅助：`sub_142693BD0 / sub_1426C46E0`

### 本轮新增：`CAkFilePackageLowLevelIO<CAkDefaultIOHookDeferred, CAkDiskPackage>` 已被静态识别

- 继续沿 `%u.wem` 解析链下钻后，当前已经能把相关对象明确收束到 Wwise 的文件定位/低层 IO 复合类：
  - `off_145FB6C00 -> CAkFilePackageLowLevelIO<CAkDefaultIOHookDeferred, CAkDiskPackage>::IAkFileLocationResolver` 视图
  - `off_145FB6C08 -> 同一对象的 IAkLowLevelIOHook` 视图
- 该对象在 Wwise 初始化 `sub_142691640` 里被注册到：
  - `AK::StreamMgr::SetFileLocationResolver(off_145FB6C00)`
  - `AK::StreamMgr::CreateDevice(..., &off_145FB6C08, &dword_145FB6C28)`
- 这说明 `%u.wem` 这条线当前已经不只是“包内 helper”，而是已经落到 Wwise 全局文件定位器本体。

### 本轮新增：`sub_142693510` 更像真正的 resolver 入口

- 从 `IAkFileLocationResolver` 视图的虚表顺序看，`sub_142693510` 位于更靠前的公共入口槽位，而 `sub_1426932B0` 位于更后的内部查找槽位。
- `sub_142693510(a1, request, outResult)` 当前静态上更像“单请求解析入口”：
  - 先调用 `(*vtable + 56)`，也就是同对象上的 `sub_1426932B0`
  - 若返回 `1`，则直接把结果对象回填给调用方
  - 若查包失败，但策略位允许继续尝试，则它会再分配一个 80 字节结果对象
  - 随后按 `request + 8` 的 `fileId` 去全局 `qword_1461C4620` 有序表二分查找并填充该结果对象
- 当前 `qword_1461C4620` 也已静态核对，不是 resolver 实例本身，而是由 `sub_142692880()` 初始化出来的全局 `fileId -> 条目指针` 查找表。
- 因而，从“外部 WEM 重定向”角度看，`sub_142693510` 比 `sub_1426932B0` 更像真正的 Wwise 文件定位边界：
  - `sub_1426932B0` 负责包内 `%u.wem` / `fileId` 解析
  - `sub_142693510` 负责把失败的包查找进一步收束成最终返回给 Wwise 的结果描述对象

### 本轮新增：`sub_142692C90` 是批量 resolver 包装层

- `sub_142692C90(a1, count, requests)` 当前静态上更像批量打开/批量解析入口：
  - 第一轮先对每个请求调用 `(*vtable + 56)`，也就是 `sub_1426932B0`
  - 若返回 `66`，且策略位允许后备路径，则把该请求暂存进 unresolved 列表
  - 第二轮再对 unresolved 请求调用 `(*vtable + 24)`，也就是 `sub_142693510`
- 因而如果后续做运行时观察或真实重定向，只挂 `sub_142693510` 理论上就已经能覆盖：
  - 单请求的 resolver 路径
  - 批量路径里所有落到后备解析的请求

### 本轮新增：对 `%u.wem` 线的 hook 优先级再收束

- 结合本轮静态证据，当前更像“最适合外部 WEM 重定向”的优先级应修正为：
  - 主候选：`sub_142693510`
  - 次候选：`sub_1426932B0`
  - 辅助观察：`sub_1426C3B10`
- 选择 `sub_142693510` 作为主候选的原因是：
  - 它已经位于 Wwise `IAkFileLocationResolver` 的公共入口边界
  - 它会统一收拢包内查找失败后的 `fileId` 后备解析
  - 其返回值已经是要交回 Wwise 的结果对象，天然比单纯 `%u.wem` 命名 helper 更接近真正的重定向插入层
- 同时，`sub_142692A10 / sub_142692B20 / sub_142692BE0` 当前更像文件包装载/卸载管理接口，而不是直接服务单首音乐加载的首选 hook 点。
- 注：后续运行时日志已经支持这一静态优先级判断；主知识库现已记录 `sub_1426932B0 -> sub_142693510(resultKind=global.fileId)` 的稳定成功路径。本节保留为静态依据与下钻背景。

### 本轮新增：`sub_142693510` 成功结果对象之后的静态消费链

- 继续沿 `CAkFilePackageLowLevelIO<CAkDefaultIOHookDeferred, CAkDiskPackage>` 的另一半虚表下钻后，当前更像的分层是：
  - resolver 层：`sub_1426932B0 / sub_142693510`
  - 低层 I/O 层：`sub_142692DE0 / sub_142692E90 / sub_142692C90 / sub_1426C4120 / sub_142692EE0 / sub_142692F60`
- `sub_142692C90` 当前静态上更像 `BatchOpen / 批量解析包装层`：
  - 第一轮逐项调用 `(*vtable + 56)`，也就是 `sub_1426932B0`
  - 对返回 `66` 且允许后备的请求，再调用 `(*vtable + 24)`，也就是 `sub_142693510`
  - 成功后会把结果对象写入 `request + 40`
  - 然后调用请求对象自己的 `(+24)` 回调，通知该请求继续推进
- 这说明从控制流上看，`sub_142693510` 的直接消费者不是某个单独的“open 文件”函数，而是：
  - 先由 `sub_142692C90` 把结果对象塞回请求
  - 再由请求对象回调继续流转
- `sub_1426C4120` 当前静态上更像 `BatchRead / 批量读提交层`：
  - 它会从每个传输描述里取出 `result + 48` 指向的条目/后端对象
  - 以及 `result + 56` 这一基址/偏移字段
  - 然后把这些信息交给 `off_14407FB20[4]`，并挂上 `sub_1426C4110` 作为完成回调
- 结合 `sub_142693510` 对结果对象的填充方式看，当前更像的链是：
  - `sub_142693510` 负责把 `fileId` 解析成 `AkFileDesc` 风格的结果描述
  - `sub_142692C90` 负责把这个描述塞回 open request
  - `sub_1426C4120` 才是更接近真实底层读提交的位置
- 注：后续运行时日志已直接看到 `sub_142693510` 的 `outObject` 紧接着在 `sub_1426C4120` 中以 `desc0 / desc1` 形式出现；因此本节主干消费链已获运行时支持。待验证部分当前只保留“`sub_1426C4120` 之后谁是真正低层打开点”这一剩余问题。

### 本轮新增：对低层 I/O 半边职责的静态判断

- `sub_142692DE0` 当前更像 `Close / 释放结果描述`：
  - 若 `a2[8] != 0`，则更像关闭一个自带后端对象的描述
  - 否则若 `a2[4] != 0`，则会委托 `qword_14619D918` 的 `+48` 方法去做后续释放/关闭
  - 同时还会对 `a2[6] + 241` 做一次 `_InterlockedDecrement8`
- `sub_142692E90` 当前更像 `GetBlockSize`：
  - 若 `desc + 64` 非空，则返回 `desc + 72`
  - 否则返回 `1`
- `sub_142692F60` 仍然更像 `OutputSearchedPaths / 调试路径输出`
- 这说明 `sub_142693510` 产出的结果对象在当前设计里至少存在两种形态：
  - 一种更像包条目/包后端驱动的描述
  - 另一种更像委托给 `qword_14619D918` 这一共享全局流设施去收尾的描述

### 本轮新增：`qword_14619D918` 的静态降级判断

- `qword_14619D918` 当前已静态确认不是 Wwise 私有的 resolver 对象：
  - 它会在 `sub_140119CF0` 里经 `sub_14206ED10` 被初始化
  - 也会在 `ObjectStreamingReader` 初始化链 `sub_1426E47A0` 里经 `sub_1420721B0` 被写回/复用
- 其交叉引用横跨对象流、资源流与本次 Wwise 文件定位链，因此它当前更像共享的全局流设施，而不是“额外音乐重定向”的首选主 hook 点
- 基于当前静态证据，更合理的优先级仍然是：
  - 主边界：`sub_142693510`
  - 更低层读提交观察点：`sub_1426C4120`
  - `qword_14619D918` 只保留为后续必要时再核对的共享后端，不作为当前主线实现目标

### 本轮新增：对“额外 WEM 重定向”实现策略的静态偏向

- 如果目标是把目录里的额外 WEM 接进游戏，而不是复刻完整 package 读链，当前静态上更偏向：
  - 继续把 `sub_142693510` 作为主重定向候选
  - 尽量避免直接落到 `sub_1426C4120` 这种已经默认“结果对象里带的是 package/entry 后端”的读提交层
- 当前一个更值得后续验证的实现方向是：
  - 不是伪造 package entry 形态
  - 而是尝试构造/借用 `sub_142692DE0` 已经能识别并委托给 `qword_14619D918` 收尾的那一类“非 package 描述”形态
- 这仍然只是静态偏向，不得写入主知识库；后续需要运行时或更深静态证据确认。
