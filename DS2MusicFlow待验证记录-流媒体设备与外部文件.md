# DS2 音乐流程待验证记录：流媒体设备与外部文件

本页只整理“外部文件能力是否真实存在，以及它现在和音乐链接到了哪一步”的静态证据。

## 已确认的设备能力

- `qword_14619D918` 背后的基础设备已静态确认是 `NXStorageReadDevice`。
- `NXStorageReadDevice + 0x18 -> sub_14206FC10`。
- `sub_14206FC10` 会：
  - 遍历输入文件名列表
  - 通过 `sub_1400C2C50` 解析逻辑名
  - 走 `MultiByteToWideChar`
  - 调 `sub_1427FE080`
- `sub_1427FE080` 下游已明确命中：
  - `GetFullPathNameW`
  - `CreateFileW`
  - `GetFileSizeEx`
- 因此，“游戏内部存在并使用按路径打开外部文件的能力”已经是硬证据。

## 已确认的启动期外部文件装载

- `sub_1426E47A0` 会在启动阶段加载 `streaming_graph.core`。
- 当前已在实际安装目录定位到该文件：
  - `F:\\SteamLibrary\\steamapps\\common\\DEATH STRANDING 2 - ON THE BEACH\\LocalCacheWinGame\\package\\streaming_graph.core`
- `StreamingGraphResource` 的反射成员表当前已确认：
  - `+0x20 = IsPacked`
  - `+0xD0 = Groups`
  - `+0x150 = Files`
- `Files` 的反射类型当前已确认是：
  - `Array_Filename`
- `sub_1426E47A0` 会把 `StreamingGraphResource + 0x150` 传给当前设备的 `+0x18` 槽位去打开文件列表。

## 已确认的文件列表内容

- 直接扫描 `streaming_graph.core` 文件内容后，已能看到大量逻辑条目，例如：
  - `cache:package/package.00.00.core`
  - `cache:package/package.40.00.core.stream`
  - `cache:package/l200_aus/package.40.08.core.stream`
- 到目前为止，尚未在这个文件里直接扫到：
  - `cache:streams/...`

## 已确认的设备表关系

- `sub_14206FC10` 成功打开文件后，会把“原始逻辑键 -> 已打开文件对象”插入设备内部表 `+0xA68`。
- 运行时查找侧的 `sub_14206FEF0` 也会从同一个 `+0xA68` 表里按字符串键查对象。
- 这说明：
  - 启动期文件表装载
  - 运行时请求查找
  当前至少共用同一套设备内表。

## 已确认的包装设备初始化

- `sub_1420721B0` 是 `DecompressingReadDevice` 构造函数：
  - 会写入 `DecompressingReadDevice::vftable`
  - 会把底层基础设备保存到 `device + 0x40`
  - 会初始化后续 I/O 线程与解压线程要用的队列和同步对象
- `DecompressingReadDevice` 当前已确认的关键槽位包括：
  - `+0x18 -> sub_142072180`
  - `+0x20 -> sub_142073210`
  - `+0x28 -> sub_142073360`
  - `+0x30 -> sub_1420733A0`
  - `+0x38 -> sub_1420734A0`
- `sub_142072180` 不做键转换，只是把“打开文件列表”继续转发给底层基础设备的 `+0x18`。
- `sub_142073210` 是真正重要的请求入口：
  - 它会先查 `device + 0x20` 处的一张分类表
  - 若对应条目为未压缩直通，则继续转发到底层基础设备 `+0x20`
  - 若对应条目为压缩块流，则会分配包装层操作对象并进入包装层自己的 I/O / 解压链

## 已确认的 streaming_graph 注入点

- `sub_1426E47A0` 在条件成立时，不只是把当前设备替换成 `DecompressingReadDevice`。
- 它还会在打开 `Files` 之后调用：
  - `sub_142072C40(v50, &v68, &v67)`
- 这里传入的两个 16 字节描述符，当前已确认来自 `StreamingGraphResource` 的两组字段：
  - `+0x360 / +0x352`
  - `+0x376 / +0x368`
- `sub_142072C40` 会：
  - 初始化 `DecompressingIOThread` 与 `DecompressionThread`
  - 把第一组描述符拷贝到 `device + 0x20`
  - 把第二组描述符拷贝到 `device + 0x30`
- 这说明 `DecompressingReadDevice` 用来决定“请求如何拆成包文件偏移与块信息”的两张核心表，直接来自 `streaming_graph.core` 载入后的资源内容。

## 已确认的包装层消费方式

- `sub_142074380` 是 `DecompressingIOThread` 的关键工作函数。
- 它会使用 `device + 0x20` 指向的表，按：
  - 请求里的 `fileIndex`
  - 当前块的 `blockIndex`
  算出底层包文件的压缩偏移与压缩长度。
- 算出偏移后，它会构造新的底层请求，再调用基础设备的 `+0x20` 去真正读包文件数据。
- `sub_142073AE0` 会同时使用：
  - `device + 0x20`
  - `device + 0x30`
  来计算：
  - 压缩偏移
  - 压缩长度
  - 解压后长度
- 它在解压失败路径里还会输出包含 `file`、`block index`、`compressed offset`、`compressed length`、`decompressed length` 的 fatal 日志。
- 这已经是硬证据：包装层并不是空转代理，而是真的在用 `streaming_graph.core` 提供的映射表，把高层请求翻译成“包文件 + 偏移 + 解压信息”。

## 当前最重要的未解问题

- 音乐链当前提交的是 `cache:streams/...`。
- 启动期外部文件表当前已看到的是 `cache:package/...`。
- 当前已经可以确认二者之间存在一个真实的中间翻译层，即 `DecompressingReadDevice`。
- 当前还新增确认了一点：包装层真正拿来索引 `streaming_graph` 表的是“请求对象的 `+0x08` 字段”。
- 但音乐 low-level 构造请求时，这个位置又静态写成了 `-1`。
- 但目前还没有证据直接证明：
  - 音乐链里出现的 `cache:streams/...` 键就是这张表里的输入键
  - 包装层最终命中的底层文件键一定就是 `streaming_graph.core` 里肉眼可见的 `cache:package/...`
- 因而当前最具体的未解点已经收敛成：
  - `streaming_graph` 表是否对 `-1` 有哨兵项或偏移基址
  - 还是说音乐请求在运行时会绕开包装层那条按索引取表的路径
- 换句话说，“中间层存在”已证实，但“音乐键如何精确映射到包文件键”还没有静态闭环。

## 当前可以成立的临时结论

- 可以确定：外部文件能力真实存在，而且已接入流媒体系统。
- 可以确定：启动阶段确实会从外部 `streaming_graph.core` 读出一批文件条目并打开。
- 可以确定：`DecompressingReadDevice` 会消费 `streaming_graph.core` 提供的两张映射表，并把高层请求翻译成底层包文件读取。
- 不能确定：当前音乐请求里的 `cache:streams/...` 已经可以被我们直接等同成某个外部磁盘路径，或直接等同成某个 `cache:package/...` 条目。
- 不能确定：把用户目录中的任意 `.wem` 放进某目录后，现有音乐链就能直接读取。

## 旧原型的运行时证据

- 当前已经做过一轮“高层选曲 + `sub_1426E7CC0` 覆写输出缓冲”的旧原型验证。
- 这一轮里已经看到：
  - `sub_140C12580 / sub_140C15560` 能稳定拿到目标 `trackId`
  - `externalMusic armed` 已经出现，说明外部 `.wem` 选择层本身工作正常
- 但同一轮运行日志里没有看到：
  - `hit sub_1426E7CC0`
  - `externalMusic override hit`
- 同时，实机听到的仍然是原版音乐，而不是目录里的外部 `.wem`。
- 因此现在可以确定：
  - 旧原型只证明了“高层已经选中了外部文件”
  - 没有证明外部字节真正进入了播放器链
  - `sub_1426E7CC0` 不是当前可接受的注入点候选，或者至少当前覆写条件没有命中真实音乐数据路径
