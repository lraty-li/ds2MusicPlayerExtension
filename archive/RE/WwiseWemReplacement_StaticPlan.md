# Wwise `.wem` 音频替换：纯静态可行主方案（IDA 证据）

目标：以“音乐替换”为第一阶段目标，在不依赖运行日志、不依赖现有 hook 代码逻辑的前提下，仅用 IDA 静态证据定位一条**必然可实施**的字节替换边界。

本文所有结论均为 **静态候选**（仅由 IDA 静态分析支持）；`运行已命中`/`已验证边界` 需要后续运行日志闭环。

---

## 1. 从播放器入口到 Wwise 资源入口（静态可证明）

### 1.1 正式播放入口 `sub_140C12580`

静态可见行为：

- 从 `a2` 取到条目对象 `trackObject = *(QWORD*)(a2 + 0x10)`。
- 从 `trackObject` 取到一个资源对象指针 `soundResource = *(QWORD*)(trackObject + 0x40)`。
- 将该资源对象指针传入运行时对象创建：`sub_140AC5210(soundResource)`。

证据等级：静态候选（可由 `sub_140C12580` 反编译直接读出字段与调用关系）。

### 1.2 试听入口 `sub_140C15560`

静态可见行为：

- 从 `a2` 取到条目对象 `trackObject = *(QWORD*)(a2 + 0x10)`。
- 从 `trackObject` 取到一个资源对象指针 `trialSoundResource = *(QWORD*)(trackObject + 0x48)`。
- 将该资源对象指针传入运行时对象创建：`sub_140AC5210(trialSoundResource)`。

证据等级：静态候选（可由 `sub_140C15560` 反编译直接读出字段与调用关系）。

### 1.3 资源对象进入 Wwise 运行时实例 `sub_140AC5210 -> sub_142684A30`

`sub_140AC5210(resource)` 静态可见关键路径：

- 调用工厂：`sub_142684A30(qword_14A10C580, 0, resource, 0)`。
- 若返回非空实例 `instance`：
  - 构造字符串 `"_on_start_"`。
  - 对 `instance` 做两次虚调用：`vftable+0x68`（取/注册事件 id）与 `vftable+0x50`（触发开始）。

`sub_142684A30(...)` 静态可见关键路径：

- 对 `resource` 做一次虚调用：`resource->vftable + 0x20` 返回 `instance`。
- 调用 `sub_142684AE0(instance, ..., resource, flags)`，将 `resource` 挂到 `instance` 内部（`a2[46] = a3`）。

证据等级：静态候选（可由 `sub_140AC5210/sub_142684A30/sub_142684AE0` 反编译直接读出调用与字段写入）。

---

## 2. Wwise 低层 IO 的“必然落点”与替换边界

本项目要替换“可听字节流”，最小三元组要求：

- 输出缓冲（`outBuf`）
- 写入长度（`len`）
- 逻辑偏移/文件偏移（`offset`）

### 2.1 Wwise 初始化把 FileLocationResolver/IOHook 指向同一个全局对象（静态可证明）

在 `sub_142691640`（Wwise 初始化）中静态可见：

- `AK::StreamMgr::Create(...)` 创建 StreamMgr。
- 若未设置 FileLocationResolver，则调用 `AK::StreamMgr::SetFileLocationResolver(...)`。
- 调用 `AK::StreamMgr::CreateDevice(..., &off_145FB6C08, &dword_145FB6C28)` 创建 streaming device。

同时，静态读取全局对象 `off_145FB6C00` 的内存布局可见：

- `0x145FB6C00 + 0x00` 是 `IAkFileLocationResolver` vtable 指针：`0x143438CD0`
- `0x145FB6C00 + 0x08` 是 `IAkLowLevelIOHook` vtable 指针：`0x143438C60`

证据等级：静态候选（由 `sub_142691640` 反编译 + 读取 `0x145FB6C00` 静态初值可得）。

意义：只要 Wwise 走 streaming 读路径，最终必然通过该对象的虚表槽位落到具体实现函数；因此我们可以围绕这些实现函数建立“可替换”边界。

---

## 3. 唯一推荐的“静态可行主方案”

主方案只围绕 Wwise `.wem` streaming 的 IO 边界推进：

1. **关联键（wemId）捕获点：** `CAkFilePackageLowLevelIO_OpenFile`（原 `sub_1426932B0`）
2. **字节替换边界（buffer/len/offset）：** `SubmitWemSegmentReadRequests`（`sub_1426C4120`）

两者同属于 `off_145FB6C00` 这一个 LowLevelIO 对象的虚表实现，因此属于同一条主方案骨架，而不是两条互相独立的假设。

### 3.1 关联键捕获点：`CAkFilePackageLowLevelIO_OpenFile`（`0x1426932B0`）

静态可证明事实：

- 当 `*(QWORD*)openData == 0` 时，该函数会执行：
  - `swprintf(L"%u.wem", *(unsigned int *)(openData + 8))`
- 即 `openData + 0x08` 是一个 **WEM 文件 id（候选 wemId）** 的来源。

该点可提供的东西：

- `wemId`（静态上可直接证明来自 `openData+8`）

该点缺失的东西（因此不能直接做字节替换）：

- 看不到输出缓冲
- 看不到读长度/偏移（只有打开/定位阶段的信息）

证据等级：静态候选。

### 3.2 字节替换边界：`SubmitWemSegmentReadRequests`（`0x1426C4120`）

这是一个批量提交“分段读请求”的实现。关键点：它在一次循环内就能凑齐替换所需三元组。

#### 3.2.1 可由汇编精确证明的字段来源

对每个 24 字节元素（步长 `0x18`），代码执行：

- `segmentObj = *(QWORD*)(elem + 0x00)`（寄存器 `rbx`）
- `transferCtx = *(QWORD*)(elem + 0x10)`（寄存器 `r9`）
- `streamObj = *(QWORD*)(segmentObj + 0x30)`（寄存器 `rdx`，随后 `rdx += 0x10` 作为参数）

然后计算：

- `offset = *(DWORD*)(transferCtx + 0x00) + *(DWORD*)(segmentObj + 0x38)`
- `len    = *(DWORD*)(transferCtx + 0x0C)`
- `outBuf = *(QWORD*)(transferCtx + 0x10)`

并将 `outBuf/offset/len` 作为参数（含结构体参数）提交给下游 submitter（`off_14407FB20->vftable[4]`）。

此外，该函数还会：

- 写 `*(QWORD*)(transferCtx + 0x28) = (this - 8)`，即把 `CAkFilePackageLowLevelIO` 的“完整对象指针”回填到 ctx（用于回调/归属）。
- 将 submit 返回的句柄写回：`*(QWORD*)(segmentObj + 0x28) = outStreamHandle`

#### 3.2.2 该边界满足闸门的点

- 输出缓冲：`outBuf = *(QWORD*)(transferCtx + 0x10)`
- 写入长度：`len = *(DWORD*)(transferCtx + 0x0C)`
- 文件偏移：`offset = *(DWORD*)(transferCtx + 0x00) + *(DWORD*)(segmentObj + 0x38)`
- 关联键（静态候选）：`streamHandle = *(QWORD*)(segmentObj + 0x28)`（由 submit 输出）

证据等级：静态候选。

#### 3.2.3 静态证明的中断点（必须明确）

在 `SubmitWemSegmentReadRequests` 内部：

- 我们能静态证明 `streamHandle/segmentObj/transferCtx` 的存在与三元组的来源。
- 但**无法仅靠静态证明**：`segmentObj/streamObj/transferCtx` 内是否直接携带 `wemId`，以及 `wemId` 与“音乐播放器某条目资源”的对应关系。

因此，**该点在本轮只能被表述为“静态可行替换边界”**，不能写成“已验证边界”。

---

## 4. 哪些点即使命中也不得直接做替换（静态约束说明）

以下点在静态上不满足“最小三元组”，因此即便运行时命中，也只能作为观测/关联点：

- `CAkFilePackageLowLevelIO_OpenFile (0x1426932B0)`：有 `wemId`，无 `outBuf/len/offset`。
- 任何只做 `resolver/submit` 分发、但拿不到 `outBuf/len/offset` 的虚调用边界：只能观测，不得做字节覆写。

---

## 5. 下一轮运行时验证：唯一优先确认的边界

优先确认 `SubmitWemSegmentReadRequests (0x1426C4120)`：

- 它是当前静态上唯一已经满足“替换三元组”的点。
- 运行时只要命中一次，就能进一步检查：
  - `segmentObj/streamObj/transferCtx` 是否可稳定提取 `wemId` 或其它可绑定资源的键
  - 以及 `outBuf` 是否确实进入可听链路（为后续升级到“已验证边界”做准备）

若该点在完整目标流程里零命中一次，应按规则降级本方案并改找其它字节写回边界。

