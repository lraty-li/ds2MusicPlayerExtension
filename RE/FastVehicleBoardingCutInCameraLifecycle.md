# 玩家上车 DSCutInCamera 生命周期

## 范围与证据

本文记录当前版本 `DSCutInCamera` 的静态生命周期，以及它与玩家
`RideOn -> Drive` 状态链的已验证边界。

证据来自当前 DS2 IDA 数据库的定点反编译、反汇编、定向交叉引用、RTTI/COL
检查和同一 `DSCameraModule` 接口下其他相机类的槽位对照。没有使用全局查找，
没有读取 `DS2.exe` 文件，也没有进行运行时状态修改。

## 对象与 RTTI

`DSCutInCamera` vtable 位于 `0x143245018`，`vtable[-1]` 指向 COL
`0x14359B968`。COL/CHD 给出的结构为：

- COL `signature=1`、`offset=0`、`cdOffset=0`；
- CHD 只包含 `DSCutInCamera` 与 `DSCameraModule`；
- 两个 BCD 都是 `mdisp=0, pdisp=-1, vdisp=0`。

因此这是从 `DSCameraModule` 开始的单一、非虚继承。虚函数收到完整
`DSCutInCamera*`，没有多继承 `this` 校正。构造分配与删除路径共同确认完整对象大小
为 `0x2A0`。

必须区分两个对象：

- `DSCutInCamera`：实际 CameraModule，大小 `0x2A0`；
- `g_DSCutInCameraRequestBroker`：线程安全的请求/当前 action 控制块，锁位于
  `+0x350`，不是 `DSCutInCamera` 子对象。

## vtable 生命周期

| slot | 地址 | 当前命名 | 已验证语义 |
|---|---|---|---|
| 0 | `0x140E228B0` | `DSCutInCamera_ScalarDeletingDestructor` | 删除析构，不是正常停止接口 |
| 2 | `0x140E22920` | `DSCutInCamera_Shutdown` | 模块 shutdown/teardown |
| 3 | `0x140E229C0` | `DSCutInCamera_Update` | 消费请求，并向 CameraMode 报告模块是否仍有效 |
| 4 | `0x140E248B0` | `DSCutInCamera_Activate` | 激活当前 action/variant |
| 5 | `0x140E24E00` | `DSCutInCamera_Deactivate` | 正常退出并完整清理副作用 |
| 6 | `0x140E277B0` | `DSCutInCamera_GetModuleTypeId` | 返回相机模块类别 `198` |
| 9 | `0x140E25700` | `DSCutInCamera_UpdatePlayback` | 推进播放并判定完成 |
| 10 | `0x140E27570` | `DSCutInCamera_PostUpdateCameraMotion` | 输出相机运动、yaw/pitch 派生量 |

slot 3/4/5 的 query/activate/deactivate 布局也出现在其他 `DSCameraModule`
派生类中，支持上述生命周期命名。

## RideOn 排队请求

`DSPlayerVehicleRideOnState_ProcessVehicleAttach` 在 `0x140F9AFCC` 调用：

```cpp
DSCutInCameraRequestBroker_QueueAction(
    g_DSCutInCameraRequestBroker,
    selectedActionHash,
    rideKind,
    8,
    vehicleTarget,
    false);
```

`DSCutInCameraRequestBroker_QueueAction` 位于 `0x140E21880`。反汇编确认：

- 第三个参数 `rideKind` 在 callee 中未被读取；
- `broker+0x1E = 1` 标记 pending request；
- `broker+0xE8` 保存 action hash；
- `broker+0xF0` 保存 request flags；
- `broker+0xF4` 设为 `-1`，表示未指定 variant；
- `broker+0xC8` 通过 intrusive observer 保存 `vehicleTarget+0x20`；
- 写入新 target 前先解除旧 observer，写入后注册新 observer；
- `bypassGlobalBlockMask=false` 时，请求会受全局阻断位限制。

`0x140E21970` 不是函数。它位于 `0x140E2196D` 的
`ReleaseSRWLockExclusive` 调用指令编码内部，没有代码引用，不能再解释为完成信号、
停止 API 或回调。

## 请求消费与 variant 选择

`DSCutInCamera_Update` 是 vtable slot 3。它在 broker 锁内读取并清除 pending
状态，然后：

1. 读取 action hash、request flags 和 requested variant index；
2. 在玩家 action resource 数组中按 hash 查找资源；
3. 合并资源 flags 与请求 flags；
4. 调用 `DSCutInCamera_SelectActionVariantForTarget`；
5. 写入模块当前 action 字段。

`DSCutInCamera_SelectActionVariantForTarget` 位于 `0x140E22F30`。其第四参数是
variant index 的输入/输出指针。它构造 `DSCameraBroadPhaseCollisionFilter` 和
`cutincam::MyHitFilter`，把相关实体与 pending vehicle target 纳入过滤，再按碰撞
可用性、距离、flags、指定索引和回退规则选择实际 variant。失败时返回空指针并把
variant index 写为 `-1`。

### CameraModule 当前 action 字段

| 偏移 | 已验证含义 |
|---|---|
| `+0x38` | 请求/期望 action hash |
| `+0x3C` | 当前 active action hash |
| `+0x40` | 合并后的当前 action flags |
| `+0x44` | 当前 selected variant index |
| `+0x48` | 激活时从 broker 取出的状态值；更细业务语义未命名 |
| `+0x50` | 当前 action resource |
| `+0x58` | 当前 selected action variant |
| `+0x190` | elapsed time |
| `+0x194` | duration |
| `+0x250` | active |
| `+0x251` | finished |
| `+0x258` | 模块内部 action 切换/重新激活 pending |
| `+0x260` | 32 字节 runtime-entry vector |
| `+0x278` | 接收激活/退出通知的 related entity observer |

## 激活

`DSCutInCamera_Activate` 完成以下关键操作：

- `this+0x3C = this+0x38`，固化 active action hash；
- `this+0x190 = 0`，清累计播放时间；
- 根据 variant 设置 `this+0x194` duration；
- 把 action hash 写入 `broker+0x138`；
- 取走 `broker+0x13C` 的一次性状态值并将其设为 `-1`；
- 调用 `DSCutInCamera_BuildRuntimeEntries(this+0x260, selectedVariant)`；
- 调用 `DSCutInCamera_SetRelatedEntityNotificationActive(this, true)`，安装
  `this+0x278` observer 并发送两条 `MsgDsNotify`；
- 根据 action flags 建立全局相机、玩家控制和通知副作用；
- 设置 `this+0x250` active，并清理/初始化相邻运行状态。

`this+0x260` 是从当前 variant 建立的 32 字节 runtime-entry vector。每项保存外部
handle、时间参数和注销状态；其业务子系统名称尚不能由当前静态证据进一步确定。

## CutIn 自身完成条件

`DSCutInCamera_UpdatePlayback` 每帧将 delta 累加到 `this+0x190`，并以
`this+0x194` 为当前 action duration。普通非循环、非保持路径到达 duration 后，
在 `0x140E27412` 设置：

```text
this+0x251 = 1  // finished
```

函数还包含受 action flags 控制的末帧保持、时间钳制、提前完成和独立倒计时路径。
这些路径最终仍写 `this+0x251` 或保持 action 活动。

该函数没有调用：

- `ActionParams_QueryBoolEventByParamId` (`0x140DBEA20`)；
- `GraphAnimationManager` bool-event 查询 (`0x140226EE0`)。

因此 `DSCutInCamera` 的完成条件来自自身 elapsed/duration 和 action flags，独立于
RideOn Update 查询的 ActionGraph `0xED` 事件。

当已激活的 CutIn 收到新 action 时，slot 3 设置 `this+0x258`。slot 9 消费该位，
在同一 `DSCutInCamera` 模块内重新调用 slot 4，再清除 `+0x258`。这是模块内部 action
切换，不经过 CameraMode 的旧模块 Deactivate。

## CameraMode 退栈与相机控制权返还

finished 后，下一次 `DSCutInCamera_Update`：

1. 调用 `DSCutInCamera_ClearRuntimeEntries(this+0x260)`；
2. 返回 `false`，表示该 CameraModule 不再有效。

`DSCameraMode_SelectAndTransitionModule` (`0x140F1AC80`) 逐项调用 CameraModule
slot 3，选择第一个返回 `true` 的模块。若选择结果与当前模块不同：

1. `0x140F1AD14` 调用旧模块 slot 5；
2. `0x140F1AFED` 调用新模块 slot 4；
3. 保存新的当前模块。

所以正常退出链为：

```text
CutIn elapsed/duration 完成
  -> this+0x251 = finished
  -> DSCutInCamera slot 3 返回 false
  -> CameraMode 调用旧模块 slot 5 Deactivate
  -> CameraMode 激活下一可用 CameraModule
```

静态证据能确认“下一可用 CameraModule”，但不能把玩家乘车场景中最终选中的具体
派生类无条件命名为某一个驾驶镜头类。

## 正常 Deactivate 清理

`DSCutInCamera_Deactivate` 是完整的正常退出点，执行：

- 按 action flags 恢复进入 CutIn 前的玩家、相机和移动控制状态；
- 解除 `broker+0xC8` vehicle target observer，清空该指针；
- 将 `broker+0x138` active action hash 设为无效值；
- 内联执行 `SetRelatedEntityNotificationActive(false)` 的反向语义：解除
  `this+0x278` observer，并向原实体发送两条对应 `MsgDsNotify`；
- 清 `this+0x58` selected variant；
- 将 `this+0x3C` active action hash 设为无效值；
- 清 `this+0x40` flags；
- 清 `this+0x250/+0x251` active/finished 以及相邻运行状态；
- 解除 `this+0xE0` observer；
- 再次幂等调用 `DSCutInCamera_ClearRuntimeEntries`；
- 注销激活时登记的全局 observer。

`broker+0xC8` vehicle target 与 `this+0x278` related entity 是两条不同引用，
退出时分别释放。

`DSCutInCamera_ClearRuntimeEntries` 会先注销每项有效的外部 handle、写回无效 handle
并标记已注销，再释放 vector backing storage、清 count/data。slot 3 在返回 false 前
调用一次，slot 5 再幂等调用一次。

## Shutdown 与析构边界

`DSCutInCamera_Shutdown` 会撤销全局 camera 位、释放 broker target、使 broker active
hash 无效，并清 runtime entries。它不清完整的 action/active 字段，因此属于模块级
shutdown/teardown，不是普通 action-complete 的正常退出接口。

scalar deleting destructor 只解除对象内 observer、释放 vector 内存、恢复基类 vtable，
并按删除标志释放 `0x2A0` 对象。它不负责正常停止当前 action，也不释放 broker 中的
vehicle target。

## 与 RideOn / Drive 的边界

`DSPlayerVehicleRideOnState_OnExit` (`0x140F999B0`) 只恢复 RideOn 标志、实体
delta-time group、seat transition 和车辆字段。它：

- 不访问 `g_DSCutInCameraRequestBroker`；
- 不调用 CutIn 请求函数；
- 不调用 `DSCutInCamera` slot 5；
- 不释放 CutIn vehicle target。

`DSPlayerVehicleDriveState_OnEnter` (`0x140F8EB60`) 同样不访问 CutIn broker，
也不调用请求或 Deactivate 接口。它在 `DSVehicleCoffin` 上执行的 mode `9` 调用属于
另一条车辆控制器生命周期；相邻 Drive 生命周期函数用同一虚函数恢复 mode `0`，该对象
链与 `DSCutInCamera` 无关。

因此：

- RideOn `0xED` 完成只推进玩家状态机；
- RideOn OnExit 不负责停止 CutIn；
- Drive OnEnter 不负责调用 CutIn Deactivate；
- CutIn 正常结束和相机模块切换由自己的播放完成标志与 CameraMode 生命周期完成。

## 已验证的完整上车演出关系

```text
RideOn ProcessVehicleAttach
  -> 排队 action hash + vehicle target
  -> DSCutInCamera slot 3 消费请求并选择 variant
  -> CameraMode 选择 DSCutInCamera
  -> slot 4 Activate
  -> slot 9 按自身 elapsed/duration 播放

同时：
  ActionGraph SkeletonAnimationEventTag -> 0xED -> RideOn 正常完成块 -> Drive

CutIn 自身完成：
  slot 9 finished -> slot 3 false
  -> CameraMode -> slot 5 Deactivate
  -> 释放 target/observer/runtime entries
  -> 激活下一可用 CameraModule
```

这解释了旧实验中“玩家已经坐入座位并进入 Drive，但镜头与原等待仍继续”的现象：
实体挂接、RideOn 状态完成、ActionGraph `0xED` 与 CutInCamera 播放完成是相互独立的
生命周期层。

## 2026-07-18 功能验证

当前 Mod 通过 RTTI/COL offset 0 包装 `DSCutInCamera` slot 9 和 slot 5。左前方自动
测试中，slot 9 的重复原生更新把 elapsed 从 `0.00834168` 推进到 `3.25327`，原函数
在 duration `3.25325` 处自行设置 finished。下一帧 CameraMode 调用原 slot 5；返回后
验证 active/finished、variant、hash、flags 与 switch pending 均已清理。

只有 Deactivate 后的下一 tick 才放行 RideOn `0xED`。运行顺序为 CutIn finished、
Deactivate clean、Graph event 186、Drive Enter；最早自动截图已经恢复车辆后方驾驶
镜头，原生下车与退出完整通过。
