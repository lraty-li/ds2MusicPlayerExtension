# 车辆上车逆向分析知识库

## 目标
实现跳过上车动画，玩家按 F 时直接传送到车辆座位上（类似 NPC 运输行为）。

---

## 核心发现：两条路径

### 路径 A：玩家上车（播动画）
```
ContextualActionComponent → RideVehicleActionPlugin → RideOnState::OnEnter → 播上车动画
```

### 路径 B：NPC上车（直接传送）
```
MountableComponent → StartMount (sub_1402F1EF0) → 直接挂载到座位
```

玩家走路径 A，NPC 走路径 B。目标是让玩家走路径 B。

---

## 类体系

### 玩家骑行相关类

| 类名 | vtable (IDA) | 说明 |
|------|-------------|------|
| DSPlayerVehicleRideOnState | 0x14325b730 | 上车状态（播动画） |
| DSPlayerVehicleRideOffState | 0x14325be10 | 下车状态 |
| DSPlayerVehicleDriveState | 0x14325bd28 | 驾驶状态 |
| DSPlayerVehicleEscapeState | 0x14325bbf0 | 逃离状态 |
| DSPlayerRideVehicleActionPlugin | 0x14325e1a8 | 骑行插件（管理状态机） |
| DSPlayerMarker | 0x143210048 | 玩家标记组件 |
| DSPlayerStateBase | 0x14323d718 | 状态基类 |

### NPC 挂载相关类（路径 B）

| 类名 | 说明 |
|------|------|
| MountableComponent | 挂载接收端（车辆方） |
| MounterComponent | 挂载发起端（NPC方） |
| MounterMover | 挂载移动器 |
| NetMountableComponentState | 网络同步用 |

---

## DSPlayerVehicleRideOnState vtable 详情（0x14325b730）

| 槽位 | 地址 | 函数 | 帧大小 | 功能 |
|------|------|------|--------|------|
| [0] | 0x140F52CD0 | sub_140F52CD0 | 40 | 构造函数 |
| [1] | 0x14101CD90 | sub_14101CD90 | - | 反编译失败 |
| [2] | 0x1400BD210 | WriteBytesCount::Reserve | - | 内存分配 |
| [3] | 0x1400BD210 | 同上 | - | - |
| [4] | 0x1400BD210 | 同上 | - | - |
| [5] | 0x14101CEE0 | sub_14101CEE0 | - | 析构函数 |
| [6-10] | 0x1400A10E0 | MemoryMgr::StartProfileThreadUsage | - | （未使用槽位） |
| **[11]** | **0x140F98CE0** | **sub_140F98CE0** | **440** | **OnEnter/上车动画入口** |
| [12] | 0x140F99990 | sub_140F99990 | 72 | OnExit/下车函数 |
| [13] | 0x140F99BE0 | sub_140F99BE0 | 40 | - |
| [14] | 0x140F99C40 | sub_140F99C40 | 136 | - |
| [15-19] | 0x1400A10E0 | - | - | 未使用 |

### sub_140F98CE0（OnEnter）详情

- **RVA**：`0xF98CE0`
- **签名**：`48 8B C4 48 89 58 ? 48 89 70 ? 57 41 56 41 57 48 81 EC ? ? ? ? C5 F8 29 70 ? C5 F8 29 78 ? C5 78 29 40 ? 45 33 FF`
- **参数**：`(__int64 a1 /*RideOnState指针*/, double xmm1)`
- **帧大小**：440 字节
- **调用方**：`sub_14111F920`（通过 vtable[23] / offset 184 间接调度）

**函数体逻辑概要**：
1. `a1 + 400` → 车辆实体，设置 `*400+944=0`、`*400+891=1` 等状态标志
2. `a1 + 152` → 角色实体，大量 `* |= 4` 位设置（动画触发标志）
3. `a1 + 168` → 某组件，`*(168+14704)` 检查状态值（3 或 9）
4. `a1 + 176` → 某组件，调用 `(176)+592` 和 `(176)+32(176, 5)` 方法
5. 调用 `sub_1401783C0` 查找某个实体
6. 调用 `sub_140157460` 计算座位/位置索引
7. **发送 `MsgDsBaggageEvent`**（行李事件）到实体
8. 设置大量角色动画骨骼参数（约 20 个 `* |= 4` 操作）
9. 调用 `sub_140E21970` 完成初始化
10. 设置 `*(168 + 892) = 0`

**prologue 结构**（前 16 字节）：
```
48 8B C4          mov rax, rsp
48 89 58 10       mov [rax+0x10], rbx      ; 存 rbx 到 home space
48 89 70 18       mov [rax+0x18], rsi      ; 存 rsi 到 home space
57                push rdi
41 56             push r14
41 57             push r15
```

**Hook 困难**：使用 `mov rax, rsp` frame pointer，rbx/rsi 保存在调用者的 home space（`[rsp+0x10]`、`[rsp+0x18]`）。使用 CALL 跳板时，CALL 多 push 了 8 字节返回地址，导致 home space 偏移错位。恢复函数执行时 rbx/rsi 读取垃圾值导致崩溃。**不可以通过 CALL-based detour 安全 hook 此函数。**

### sub_140F99990（RideOnState::OnExit）详情

- **RVA**：`0xF99990`
- **帧大小**：72 字节
- **功能**：清理骑行状态
  1. `a1[50] + 401/402 = 0`（清除标志）
  2. `(a1[18] + 680) + 1516 &= ~0x80000`
  3. `sub_14100FD30(a1[50])`（角色状态清理）
  4. `sub_140F8EA80(...)`（Mountable 组件清理）
  5. `sub_1410115E0(a1[50])`（车辆关联清理）
- ⚠️ 不调用 sub_140E21970 —— 不在退出时发送完成信号

### DSPlayerVehicleDriveState vtable 详情（0x14325bd28）

| 槽位 | 地址 | 函数 | 帧大小 | 功能 |
|------|------|------|--------|------|
| [0] | 0x140F8EB10 | sub_140F8EB10 | - | 构造函数 |
| [1] | 0x14101CD90 | sub_14101CD90 | - | 命令调度器（与 RideOnState 共用） |
| [11] | 0x140F8EB40 | sub_140F8EB40 | ~600 | **OnEnter/驾驶入口** |
| [12] | 0x140F8F3E0 | sub_140F8F3E0 | 72 | **OnExit/驾驶退出清理** |

### sub_140F8EB40（DriveState::OnEnter）概要

- 设置角色动画标志（`* |= 0x40000`、`0x80000`、`0x100000` 等）
- 调用 sub_141F6BDC0（位置/相机函数？）
- 调用 sub_140B198E0（动作设置）
- 设置车辆状态标志 `*(vehicle+897) |= 4`
- 设置 UpperBody 动作 `*(vehicle+160+1940) = 17`

---

## DSPlayerRideVehicleActionPlugin vtable 详情（0x14325e1a8）

| 槽位 | 地址 | 函数 | 帧大小 | 功能 |
|------|------|------|--------|------|
| [0] | 0x1410044B0 | sub_1410044B0 | 40 | 构造 |
| [1] | 0x1410047B0 | sub_1410047B0 | 152 | Init |
| [2] | 0x141008710 | sub_141008710 | 40 | - |
| [5] | 0x141004AA0 | sub_141004AA0 | 40 | 析构 |
| [6] | 0x141004F00 | sub_141004F00 | 632 | Update/Activate |
| [7] | 0x141120C20 | sub_141120C20 | 40 | - |
| [8] | 0x140FE45B0 | sub_140FE45B0 | - | - |
| [9] | 0x14100C3E0 | sub_14100C3E0 | 40 | - |
| [10] | 0x141120D40 | sub_141120D40 | 40 | - |
| [13] | 0x140DFA390 | sub_140DFA390 | - | - |
| [14] | 0x141004660 | sub_141004660 | - | - |
| [17] | 0x141009050 | sub_141009050 | 104 | - |
| [18] | 0x141009730 | sub_141009730 | 2152 | 大函数 |

### sub_141004F00（Update/Activate）详情

- **帧大小**：632 字节
- **功能**：插件主循环函数，处理骑行生命周期的状态管理
  1. 检查 `a1+280 == 0` 进行初始化
  2. 操纵大量角色/车辆状态标志
  3. 调用 `sub_14100C900`（上车位置/角度计算）
  4. 调用 `sub_141003D60`（状态配置）
  5. 调用 `sub_141014960`、`sub_141004DD0` 子函数
  6. 操作 SRWLock 保护的数据结构
  7. 调用 `atan2f` 计算朝向角度
- 此函数过于复杂，不适合直接修改

---

## 状态创建流程

### sub_14100C0B0 — 状态工厂

- 创建 RideOn/Drive/RideOff/Escape 四个状态对象
- RideOnState 存在 `a1[41]`（偏移 328）
- DriveState 存在 `a1[43]`（偏移 344）
- RideOffState 存在 `a1[44]`（偏移 352）
- EscapeState 存在 `a1[46]`（偏移 368）
- 初始化后调用各状态的 vtable[5]（初始化方法）

### sub_14111F920 — 状态调度器

- 通过 vtable[23]（offset 184）间接调用各状态方法
- 操作字节标志 `a1+280`、`a1+282`、`a1+283`
- 是 RideOnEnter（sub_140F98CE0）的直接调用方

---

## 实体初始化链

### sub_140DC5050 @ RVA `0xDC5050`

- 实体初始化函数
- 创建 DSPlayerMarker 存到 `a1+22120`
- 调用 `sub_140DC6380`（LowerBody 初始化，一次）
- 调用 `sub_140138810` 完成注册

### sub_140DC6380 @ RVA `0xDC6380`

- LowerBody 动画参数初始化（**不是每帧调用**）
- 仅在实体创建时调用一次
- 访问 `a1+200`、`a1+888`、`a1+1900-2088` 等字段

---

## UpperBody 动作状态系统

### 注册函数：sub_14002B090

- 注册 22 个 UpperBody 动作状态
- 每个状态：调用 `sub_1400A4100(data, name, len)` 存储名称
- "RideVehicle" = index 17（字符串地址 `0x1431fe260`）
- 状态表地址：`0x1462878D0`
- 注销函数：`sub_142C0D6A0`（遍历 44 项调 `sub_1400A38A0`）

### 过渡表

- `0x142dd7d80`：RideVehicle 过渡表（PoisonArea 上下文中，含 3 条过渡）
- `0x142def858`：RideVehicle 过渡表（RideFloater 上下文中）
- `0x142dfc818`：RideVehicle 过渡表（Zipline 上下文中）
- 所有过渡表无代码级 xref，仅数据引用
- 表结构：count(8) + name_ptr(8) + padding

---

## 关键字符串地址

| 字符串 | IDA 地址 | 说明 |
|--------|---------|------|
| "RideVehicle" | 0x1431FE260 | UpperBody 动作名 |
| "DSPlayerVehicleRideOnState" | 0x14613383C | RTTI 类名 |
| "DSPlayerRideVehicleActionPlugin" | 0x146134434 | RTTI 类名 |
| "DSPlayerVehicleRideOffState" | 0x1461338xx | RTTI 类名 |
| "DSPlayerVehicleDriveState" | 0x146133xxx | RTTI 类名 |

---

## Mount 系统（NPC 路径）

### sub_1402F08D0 — MountableComponent 注册

- 注册 "MountableComponent" 组件
- 导出 "GetMounter" 函数（→ sub_1402F8810）
- 注册消息处理器（→ sub_1402F38B0）

### sub_1402F38B0 — MountHandler

- 处理 MsgStartMount 消息
- 遍历挂载点，计算最近位置
- 调用 `sub_1402F1EF0`（StartMount）执行挂载

### sub_1402F1EF0 — StartMount

- **RVA**：`0x2F1EF0`
- 设置 `a1+80 = 1`（已挂载标记）
- 存储挂载目标到 `a1+88`、`a1+288`、`a1+296`
- 创建回调 `sub_1402F2040` 处理挂载完成
- **玩家上车时此函数不触发**

---

## naming 文件索引

- `all-names.txt`：275,000+ 符号名与地址（不同构建，ASLR 基址 `0x7FF67E890000`）
- IDA 当前构建基址：`0x140000000`
- RVA 可直接比对（偏移一致），但绝对地址需减去 ASLR 基址差

---

## 引擎特征

- 引擎：Decima / Fox Engine
- 线程模型：大量 SRWLock、TryAcquireSRWLockExclusive
- 消息系统：`sub_140130C60` 发送消息（如 MsgDsBaggageEvent）
- 组件查找：`sub_14011FFA0(entity, uuid)` 通过 UUID 查找组件
- 字符串管理：`sub_1400A4100` 分配/复制字符串
- TLS 管理：大量 `NtCurrentTeb()->ThreadLocalStoragePointer` + 6760 偏移

---

## 崩溃报告

- 进程名：`crs-handler`
- 窗口标题："report problem"
- 崩溃时锁定 .asi 文件，需先杀掉才能重新构建

---

## Hook 方法论（参考音乐播放器）

### 可用模式：13 字节 detour + push/pop prologue

**成功的条件：**
1. 函数 prologue 使用 `push rX` 而非 `mov [rsp+XX], rX`（自平衡，不受 CALL 栈影响）
2. 函数 prologue 在 13 字节以内
3. 不使用 frame pointer（`mov rax, rsp`）

**已验证兼容：**
- `sub_1402F1EF0`（StartMount）：`push rdi; sub rsp, 30h` — ✅ 13字节，稳定
- `SetPlayState`（音乐播放器）：`push rdi; sub rsp, 20h` — ✅ 参考实现

**不兼容：**
- `sub_140F98CE0`（RideOnEnter）：`mov rax, rsp; mov [rax+XX], rbx` — ❌ frame pointer + home space
- `sub_14111F920`（状态调度器）：`mov [rsp+30h], rbx; mov [rsp+38h], rsi` — ❌ fixed-offset stores
- `sub_140F99990`（OnExit）：`sub rsp, 48h; mov [rsp+40h], rdi` — ❌ fixed-offset + 15字节

### ASI 多文件冲突
- ASI loader 按字母序加载所有 `.asi` 文件
- 旧的 `old.asi` 会同时加载、覆盖同函数 hook
- 必须清理旧文件

---

## Plugin 状态机架构（新发现 2024-06-28，2024-06-28 修订）

### vtable 补充（DSPlayerRideVehicleActionPlugin @ 0x14325e1a8）

| 槽位 | 地址 | 函数 | 功能 |
|------|------|------|------|
| [1] | 0x1410047B0 | sub_1410047B0 | **Init** — 设置 next_state=1 触发上车（prologue 5 字节，可 detour） |
| [6] | 0x141004F00 | sub_141004F00 | **Update/Activate** — 状态检查+主逻辑（prologue 14 字节+frame ptr，不可 detour） |
| [20] | 0x14100C0B0 | sub_14100C0B0 | 状态工厂：创建 RideOn/Drive/RideOff/Escape 对象 |
| [22] | 0x14111F920 | sub_14111F920 | **状态调度器**（vtable[22] = offset 0xB0） |
| [23] | 0x140FE4560 | sub_140FE4560 | **状态过渡处理器**（vtable[23] = offset 0xB8，调度器调用此函数） |

### 状态索引与 handler 表（funcs_140FE45AA @ 0x142E0C9D0 → RVA 0x2E0C9D0）

Handler 函数是小型 trampoline，仅加载状态对象并尾调用其 vtable[1] 调度器：

| 索引 | Handler | 加载偏移 | 对应状态 | Trampoline 汇编 |
|------|---------|---------|---------|----------------|
| 0 | 0x1400A10E0 | (null) | 空/初始状态 | （空操作） |
| 1 | 0x14100AF00 | plugin[42] (0x150) | **RideOnState** | `mov rcx,[rcx+0x150]; mov rax,[rcx]; jmp [rax+8]` |
| 2 | 0x14100AF10 | plugin[43] (0x158) | **DriveState** | `mov rcx,[rcx+0x158]; mov rax,[rcx]; jmp [rax+8]` |
| 3 | 0x14100AF20 | plugin[44] (0x160) | **RideOffState** | `mov rcx,[rcx+0x160]; mov rax,[rcx]; jmp [rax+8]` |
| 4 | 0x14100AF30 | plugin[46] (0x170) | EscapeState | 复杂（带 switch） |

**关键洞察**：Handler 表在 .rdata 段（可 VirtualProtect）。改写 funcs_140FE45AA[1] 指向 DriveState handler (0x14100AF10)，即可将 RideOn 重定向为 Drive！

### 过渡处理器 sub_140FE4560 逻辑

```
1. 调用旧 handler (plugin+320) → r8=1 → case 1 → OnExit
2. 查表 funcs_140FE45AA[next_state] → 新 handler
3. 存储新 handler 到 plugin+320
4. 尾跳转新 handler → r8=0 → case 0 → OnEnter
```

### 状态调度器 sub_14111F920 逻辑

```c
if (next_state != current_state OR (next==current && flag!=0)) {
    vtable[23](plugin, current_state, next_state);  // 过渡处理器
    current_state = next_state;
    flag = 0;
}
```

### 状态字节位置

- `plugin+280 (0x118)`: 当前状态 byte
- `plugin+282 (0x11A)`: 下一状态 byte
- `plugin+283 (0x11B)`: 状态标志 byte
- `plugin+320 (0x140)`: 当前活跃 state handler 函数指针
- `plugin+544 (0x220)`: seat/mount 标识（Init 中存储）

### Init 函数 (sub_1410047B0) 详解

```c
// 路径 1: 直接上车（player state == 3 or 9）
v3 = plugin[7];                      // 玩家实体
v4 = *(v3 + 14704);                  // 玩家状态
if (v4 == 3 || v4 == 9) {
    v5 = *(v3 + 14816);              // 挂载点标识
    plugin[68] = v5;                  // 存储到 plugin+544
    if (v5 != -1) {
        *(plugin + 148632) = 1;       // 标记已上车
        *(WORD*)(plugin + 282) = 1;   // next_state=1, flag=0 → 触发 RideOn！
        return 1;
    }
}
// 路径 2: 检查条件后上车
if (sub_141011B00(plugin)) {
    // 各种条件检查...
    if (conditions_met) {
        plugin[68] = plugin[148984];
        *(WORD*)(plugin + 282) = 257; // next_state=1, flag=1 → 触发 RideOn
        sub_141014F10(plugin);
        return 1;
    }
}
return 0;
```

**Init prologue**: `push rbx; sub rsp, 90h` = **5 字节 — ✅ 可 13 字节 detour！**

### RideOnState::vtable[1] (sub_14101CD90) — 命令调度器

基于 r8 参数分发到不同 vtable 方法（jump table, 17 cases）：
- case 0 (r8=0) → vtable[11] = OnEnter
- case 1 (r8=1) → vtable[12] = OnExit

---

## PluginHelper 三次调用时序（实测数据）

| 调用 | caller RVA | 所在函数 | 插件状态 (280/282) | RideOnState |
|------|-----------|---------|-------------------|-------------|
| #1 | 0xF8F832 | sub_140F8F3E0（**DriveState::OnExit** 退出驾驶时清理） | state=2(Drive), next=3(RideOff) | 存在 |
| #2 | 0x1005CE6 | sub_141004F00（Update） | state=0, next=0 | 存在 |
| #3 | 0x1005CEE | sub_141004DD0（Update 子函数） | state=0, next=0 | 存在 |

**修正**：sub_140F8F3E0 是 DriveState 的 OnExit（在玩家**退出驾驶**时调用），不是上车完成回调。
PluginHelper #1 的调用发生在退出驾驶时（state=Drive→RideOff），而非上车完成时。

**结论**：
- ⚠️ **状态机在 PluginHelper #1 之前就已过渡到 DriveState**
- ⚠️ **上车动画（2 秒）独立于状态机运行**
- ⚠️ **PluginHelper detour 中调 `sub_140E21970(-1)` 落在 Drive 退出上下文，不是上车动画的直接控制点**

---


## 已确认边界

| 干预点 | 已确认行为边界 |
|------|------|
| 调用 OnExit (vtable[12]) | OnExit 是清理函数，不直接改写上车动画图 |
| 设置 next_state=2 + 调用调度器 | 在当前观测路径中，状态机在动画播放前已过渡，调用时机晚于可见动画装载 |
| 调用 sub_140E21970 信号完成 | `sub_140E21970` 在 OnEnter 中也被调用，其语义不是单纯“停止信号” |
| animObj 清理 + globalFlag + sub_140E21970 | 该组合当时落在 Update/state=0 上下文，不是上车动画直接上下文 |
| PluginHelper detour 中调用 sub_140E21970(-1) | 该 detour 落在 `DriveState::OnExit` 路径，不与上车动画直接对齐 |

---

## Handler 表重定向方案（🆕 当前探索方向）

### 核心思路

Handler 表 `funcs_140FE45AA`（RVA `0x2E0C9D0`）将状态索引映射到 handler trampoline：
- [1] = RideOnState handler → 加载 plugin[42] → RideOnState::vtable[1] 调度器 → OnEnter（上车动画）
- [2] = DriveState handler → 加载 plugin[43] → DriveState::vtable[1] 调度器 → OnEnter（驾驶入口）

**改写 [1] 指向 DriveState handler**，当 Init 触发 next=1 (RideOn) 时，实际执行的是 DriveState 的 OnEnter，跳过上车动画。

### 技术路径

```
1. VirtualProtect 使 handler 表所在页可写
2. 验证 table[1] == game_base + 0x100AF00（RideOn handler）
3. 写入 table[1] = game_base + 0x100AF10（Drive handler）
4. VirtualProtect 恢复保护
```

### 已知风险

Handler 表重定向后，过渡处理器仍将 plugin 状态 byte 设为 1（RideOn 编号），但 handler 已是 Drive。
后续状态流转（0→1→0？）需要验证是否会导致 DriveState::OnExit 被错误触发。

### 替代方案

- **Hook Init (sub_1410047B0)**：5 字节 prologue，可在 Init 返回后改 next_state 为 2
- **VTable 重写**：修改 RideOnState vtable[11] 指向自定义 OnEnter（更复杂）

---

## 动画完成机制（已修正）

### ⚠️ 关键修正：sub_140F8F3E0 不是 RideOnState 过渡函数

sub_140F8F3E0 实为 **DriveState::vtable[12] = OnExit**（在玩家退出驾驶时调用）。

RideOnState 的真正 OnExit 是 sub_140F99990（vtable[12]），它只做清理，不调用 sub_140E21970。

### DriveState 和 RideOnState vtable 对比

| 状态类 | vtable | [11] OnEnter | [12] OnExit |
|--------|--------|-------------|------------|
| RideOnState | 0x14325b730 | 0x140F98CE0（上车动画） | 0x140F99990（清理标志） |
| DriveState | 0x14325bd28 | 0x140F8EB40（驾驶入口） | 0x140F8F3E0（驾驶退出清理） |

### sub_140F8F3E0（DriveState::OnExit）内部

```
// 1. 清理动画/标志位
// 2. 如果 animObj 存在：
//    - *(animObj - 32 + 8523) = 0    // 清除 animObj 字段
//    - 如果 sub_140E406C0() 返回真 → sub_140E21970(-1) + 清除 global+0x2C
// 3. 调用 PluginHelper（始终执行）
```

⚠️ 此函数中的动画完成逻辑是 **Drive 退出** 时的清理，不影响上车动画。

### 全局对象 qword_14623E908

- **RVA**: `0x623E908` → runtime: `DS2.exe + 0x623E908`
- **关键偏移**: `+0x1F`(byte)=完成标志, `+0x2C`(byte)=条件标志, `+0xEC`(int32)=参数, `+0x158`(float)=参数, `+0x350`=SRWLock

### sub_140E406C0 — 完成条件检查

- **RVA**: `0xE406C0`
- 读取 `[global + 0x2C]` byte 并返回

### sub_140E21970 — 完成信号函数

- **RVA**: `0xE21970`
- **签名（已验证）**: `48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8B 3D ?? ?? ?? ?? 8B F2 C5 F8 29 74 24 20 C5 F8 28 F2 48 8D 9F 50 03 00 00`
- rcx 被忽略，以 edx/xmm2 为参数写入 global
- **在 OnEnter 中也调用** → 设置初始值，非停止信号

---

## 当前阻塞

### caller 0xF8F832 消失问题

| 测试 | PluginHelper 调用 |
|------|------------------|
| 19:27, 19:29 | #1 `0xF8F832`(sub_140F8F3E0) state=2; #2 `0x1005CE6`(Update) state=0; #3 `0x1005CEE` |
| 20:07—20:22 | #1 `0x1005CE6`(Update) state=0; #2 `0x1005CEE` — **无 F8F832** |

sub_140F8F3E0 路径在近期测试中完全不触发，但用户视觉确认动画在播放。

---

## 当前知识状态

- ✅ 13字节 detour on PluginHelper（0 崩溃）
- ✅ 状态机架构：handler 表、过渡处理器、命令调度器
- ✅ Plugin 布局：a1[42]=RideOnState, a1[43]=DriveState
- ✅ sub_140E21970 签名正确匹配
- ✅ animObj 可读（RideOnState[20]+30040）
- ✅ globalFlag 可读写（base+0x623E934）
- ✅ PluginHelper 相关完成信号目前主要落在 Drive 退出 / Update 上下文
- 🔄 sub_140F8F3E0 路径出现条件仍需继续归因
- 🔄 当前直接相关的上下文仍是 OnEnter 与动画播放期间
