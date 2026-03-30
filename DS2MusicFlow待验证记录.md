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

- 本轮静态证据下，对“原生外部文件重定向切入层”的判断已修正：
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
