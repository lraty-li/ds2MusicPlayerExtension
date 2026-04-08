# submitter 对象链静态分析：ObjectStreamingSystem 与 LowLevelIO

## 目的
- 只回答 submitter / stream cache submitter 的真实初始化与对象流。
- 不从 `0x407FB20` 固定全局槽位出发。
- 本文所有结论都属于静态分析，不把任何点写成已验证边界。

## 已能静态确认的对象链

### 1. 全局持有者先构造 `StreamingManager`
- `GameMainProg (0x1400A1120)` 通过 `sub_1400A13A0(..., sub_140691BF0)` 触发全局流媒体系统初始化。
- `sub_140691BF0`
  - 分配 `0xA05E0` 字节。
  - 调用 `sub_1426C7100` 初始化该对象。
  - 把返回值写入 `qword_1461C4638`。
- 因而 `qword_1461C4638` 是 `StreamingManager*` 的全局持有者。

### 2. `StreamingManager` 构造时内嵌持有一个 `ObjectStreamingSystem*`
- `sub_1426C7100` 先初始化 `StreamingManager` 自身。
- 随后它分配 `0x1502C0` 字节的新对象 `v10`。
- 对 `v10` 的关键写入：
  - `[v10+0x00] = ObjectStreamingSystem primary vftable`
  - `[v10+0x20] = ObjectStreamingSystem secondary vftable _0`
  - `[v10+0x28] = ObjectStreamingSystem secondary vftable _1`
  - `[v10+0x30] = 0` 初始化标记
  - `[v10+0xA8] / [v10+0xB0] / [v10+0xB8]` 初始化三个 SRWLock
- 然后：
  - `[StreamingManager+0x578] = v10`
- 所以静态可确认链为：
  - `qword_1461C4638 -> StreamingManager`
  - `[StreamingManager+0x578] -> ObjectStreamingSystem`

### 3. `InitStreamCacheSubmitterThread` 暴露的 submitter 不是固定全局对象，而是 `ObjectStreamingSystem` 的子对象
- `InitStreamCacheSubmitterThread (0x1426E4670)` 的核心写入是：
  - `off_14407FB20 = a1 + 0x20`
- 这里 `a1` 是 `ObjectStreamingSystem*`。
- 因而 `off_14407FB20` 指向的不是独立全局实例，而是：
  - `ObjectStreamingSystem` 内部偏移 `+0x20` 的二级子对象/接口子对象。
- 这与构造函数里 `[obj+0x20] = secondary vftable _0` 完整对上。

### 4. submitter 线程的启动上下文也能静态闭环到 `ObjectStreamingSystem`
- `InitStreamCacheSubmitterThread` 还会设置：
  - `[a1+0x48] = sub_1426E73C0`
  - `[a1+0x50] = a1`
  - `CreateThread(..., lpParameter = a1 + 0x38, ...)`
- `StartAddress (0x140118F40)` 接到 `lpThreadParameter` 后：
  - 读取 `[lpThreadParameter+0x18]` 作为目标对象
  - 读取 `[lpThreadParameter+0x10]` 作为入口函数
  - 实际执行 `sub_1426E73C0(a1)`
- 因而线程启动链静态可还原为：
  - `lpThreadParameter = ObjectStreamingSystem + 0x38`
  - `[lpThreadParameter+0x18] = ObjectStreamingSystem*`
  - worker 入口拿到的仍是同一个 `ObjectStreamingSystem*`

### 5. `sub_1426C4120` 与 `sub_142692C90 / DE0 / EE0 / E90` 属于另一类对象：`CAkFilePackageLowLevelIO`
- `0x143438C60` 处的 vftable 已被 IDA 标成：
  - `CAkFilePackageLowLevelIO<CAkDefaultIOHookDeferred, CAkDiskPackage>::vftable`
- 该表上的方法包括：
  - `sub_142692DE0`
  - `sub_142692E90`
  - `sub_142692C90`
  - `sub_1426C4120`
  - `sub_142692EE0`
- `sub_1426936D0` 证明了该对象是双接口对象：
  - `[a1+0x00] = IAkFileLocationResolver vftable`
  - `[a1+0x08] = IAkLowLevelIOHook vftable`
- `sub_142691640` 用 `CreateDevice(..., &off_145FB6C08, ...)` 把这个全局 low-level IO hook 交给 Wwise 设备。

### 6. `sub_1426C4120` 的 `arg0` 不是 submitter
- `sub_1426C4120` 一进来就做：
  - `v4 = a1 - 8`
- 这说明 `a1` 是 `IAkLowLevelIOHook` 子对象指针，减 8 才回到完整 `CAkFilePackageLowLevelIO` 对象。
- 之后它用 `off_14407FB20` 发起真正的提交调用。
- 也就是说：
  - `arg0` 来自 low-level IO hook 对象
  - submitter 仍然单独从 `off_14407FB20` 取
  - 静态上看不出 `arg0 -> submitter` 的对象链

## 还需要运行时验证的最后一跳

### 1. `off_14407FB20` 与 `ObjectStreamingSystem+0x20` 的运行时一致性
- 静态上已经能证明初始化代码会执行 `off_14407FB20 = objectStreamingSystem + 0x20`。
- 但仍需运行时确认：
  - 实际命中的 `InitStreamCacheSubmitterThread` 的 `this`
  - 后续 `sub_1426C4120` 命中时读到的 `off_14407FB20`
  - 这两者是否稳定相等

### 2. `+0x20` 子对象的真实虚表与槽位 `[4]`
- 静态已能确认 submitter 来源是 `ObjectStreamingSystem+0x20`。
- 仍需运行时记录：
  - `*(void**)(objectStreamingSystem + 0x20)` 的真实 vftable
  - 该 vftable 槽位 `[4]` 的真实函数指针
- 这一步没记录前，不能把任何静态命名函数写成最终提交实现。

### 3. `sub_1426C4120` 现场无法从现有参数直接反推出 submitter
- 当前静态证据只支持：
  - `arg0` 是 `CAkFilePackageLowLevelIO` 子对象
  - `items` 是请求数组
  - submitter 从 `off_14407FB20` 读取
- 还没有静态 def-use 闭环证明：
  - `arg0`
  - `items`
  - 回调上下文
  - 线程对象
  之间存在一条当前函数内可直接回到 `ObjectStreamingSystem` 的链。

## 建议的 hook 点或观测点

### 1. 首选观测：`InitStreamCacheSubmitterThread`
- 记录：
  - `this`
  - `this + 0x20`
  - `*(void**)(this + 0x20)` 的 vftable
  - `((void**)*(void**)(this + 0x20))[4]`
- 这是当前最稳的 submitter 对象来源。

### 2. 备用观测：`StartAddress` 或 `sub_1426E73C0`
- `StartAddress` 的 `lpThreadParameter` 可反推：
  - `owner = *(void**)(lpThreadParameter + 0x18)`，即 `ObjectStreamingSystem*`
  - 也可直接用 `owner = lpThreadParameter - 0x38`
- 如果 `InitStreamCacheSubmitterThread` 不方便下手，这里仍能拿到同一对象。

### 3. 对照观测：`sub_1426C4120`
- 这里只建议记录，不建议把对象来源建立在它的 `arg0` 上。
- 记录项：
  - `off_14407FB20`
  - `cachedObjectStreamingSystem + 0x20`
  - 两者是否相等
- 若稳定相等，代码里就可以从缓存的 `ObjectStreamingSystem*` 派生 submitter，而不是继续信任固定全局地址。

## 代码实现建议
- 不要再从“固定全局对象槽位”读取 submitter。
- 更稳的来源是：
  - 在 `InitStreamCacheSubmitterThread` 命中时缓存 `ObjectStreamingSystem* this`
  - submitter 指针用 `this + 0x20` 计算
  - 把 `off_14407FB20` 仅作为一致性校验日志
- 不建议从 `sub_1426C4120.arg0` 反推 submitter：
  - 该参数静态上已经落到 `CAkFilePackageLowLevelIO`，不是 submitter 所属对象。
