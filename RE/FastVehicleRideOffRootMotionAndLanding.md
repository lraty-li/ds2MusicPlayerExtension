# 快速下车：根运动与安全落点

日期：2026-08-23

返回[当前状态与知识索引](FastVehicleBoardingModImplementation.md)。

> **结论：**正常 RideOff 的约 2 秒动作与侧向/落地位移同步完成。已确认的 side 查询、
> seat transition、pose-motion 缓存、mover 外部当帧输入和
> `PhysicsCharacterMoverProxy` 当前坐标接口都不保存可直接读取的未来终点。提交 RideOff
> 前直接 detach 会消除动作，却把玩家留在座位附近并站到车体上。已验证实现改为让
> 原生 Graph 一次求值当前起点到 descriptor 末端的完整区间，原生根运动链会把玩家
> 正确带到车旁。

## ActionGraph 时间与长结果

长 descriptor、fullgame 结果传播、DS2 宿主 evaluator、结果对象时间区间以及
motion transform payload 的完整证据已移至
[ActionGraph 长结果与根运动时间区间](FastVehicleRideOffActionGraphTiming.md)。

## 侧向、mover 与 PhysicsCharacterMover 边界

### 2026-07-26 下车侧向查询边界

`DSRideRuntime_ClassifyDismountSide` 在选择 `0/1/2` 三个下车方向前调用
`DSRideRuntime_QueryDismountClearance`（`0x141012650`）。该函数从当前玩家与车辆
变换构造候选探测起点，并把参数交给 `0x141F97560`；后者使用
`PhysicsBroadPhaseCollisionFilter` 执行多组阻挡/地面式碰撞探测。四个输出参数均为
布尔值或标志字节，调用链没有向 RideRuntime 写入世界坐标，也没有返回落地点变换。

因此，这条链只回答各候选方向能否使用，并供上层返回 side index `0/1/2` 或
`0xFF`；它不是把角色安置到安全地面的端点求解器。单独复用侧向分类或其碰撞查询不能
修复提交前绕过中“瞬间离座但站在车体上”的错误落点。

RideOff OnEnter 对分类结果的后续处理也已闭合：它把 side `0/1/2` 转为 float，
当值变化时设置玩家参数包 `player+0x3880` 的 dirty bit `4`，并把数值写到
`player+0x3890`；同时把原始 side byte 保存到 `rideOff+0x1B2`。已有 RideOn
ActionGraph 静态分析确认 `+0x3890` 同一三值参数用于候选动画结果选择。这里写入的
仍只是图分支索引，不包含方向向量、距离、高度或世界变换，因此不能把 side 数值本身
当作车外坐标，也不能从 `rideOff+0x1B2` 直接恢复原生动作末端。

RideOff 选完 side/车辆动画请求后调用的
`DSPlayerRideVehicleActionPlugin_BeginSeatTransition`
（`0x14100EF70`）也不是该端点求解器。这个函数同时由 RideOn/通用初始化路径以参数
`0` 调用，RideOff OnEnter 只把参数改为 `1`；它解析座椅目标、执行 pre-attach
记账、注册 collector，随后调用
`DSPlayerRideVehicleActionPlugin_AdjustTransitionWorldTransform`
（`0x141010B70`）。完整调用链既没有接收选中的 side，也没有读取 RideOff 状态中的
side 字段。

后一个函数确实会进行空间碰撞检查，但最终只在探测分支中调用
`DSPlayerMoverAccessor_TryApplyWorldTransform`（`0x140DBACA0`，虚表
`+0x350`）或 `DSPlayerMoverAccessor_StageWorldTransform`
（`0x140DBACE0`，虚表 `+0x358`）。前者通过
`Entity_SetWorldTransformLocked` 立即提交当前修正变换；后者只把 64 字节变换复制到
mover 的 pending 区并设置 staged 标志。由于这条共享链没有 side 输入，它只能校正
当前/座椅过渡变换，不能产生正常下车所需的侧向地面端点；单独补调这条链同样不能作为
“站在车上”的修复。

紧邻其前的 `0x140F779E0` 也已排除。RideOn 与 RideOff 都用完全相同的参数
`index=15、state=1、hash=304208212` 调用它；函数只在两组按 index 划分的数组中
增加或删除该 hash，没有任何物理查询或变换读写。它是共享动作标志记账，不承载
下车方向或落地点。

`BeginSeatTransition` 通过
`DSPlayerMoverAccessor_SetRideOffCollector`（`0x140DBAAB0`，虚表
`+0x300`）只把 plugin 的 `RideOffCollector` 指针保存到 `mover+0x7E0`；
OnExit cleanup 用同一槽传入 null 清除。collector 的回调
`DSPlayerRideVehicleActionPlugin_RideOffCollector_ShouldKeepCandidate`
（`0x141003F00`）只过滤关联对象 handle 与当前车辆 handle 相同的候选，不保存
落点，也不推进或取消动画。因此 collector 是当前车辆排除过滤器，不是玩家动作入口或
落点求解器。

OnEnter 尾部的 `0x140DB4B80` 已命名为
`DSTalk_RequestPlayerVoiceEvent`。它在 SRW lock 下向 `DSTalkInternal` 队列提交
语音事件，失败分支构造的是 `MsgMissionPlayerVoiceEventNotify`；调用链不触碰 mover
姿态、动画图或世界变换。因此该调用只是下车语音记账，也已从玩家动作和落点入口中
排除。

### 2026-07-26 当前姿态运动缓存不是下车终点

`DSPlayerMoverAccessor` 虚表 `+0x400` 的实现 `0x140DBB2F0` 已命名为
`DSPlayerMoverAccessor_GetCachedPoseMotionTransform`。它只返回当前 mover 的
`+0xA70` 变换；`DSPlayerMover_UpdatePoseMotionCache`（`0x140ED2EA0`）会在每次
姿态更新中把工作变换 `mover+0x958` 复制到这个缓存，并继续执行当帧的姿态、骨骼和
地面修正。这里没有预先保存完整下车动作结束时的车外终点。

`DSRideRuntime_QueryDismountClearance` 通过该虚表槽取得 `mover+0xA70` 后，只把它与
车辆世界变换组合成当前碰撞探测的基准，再交给 `0x141F97560` 生成正向、反向及第三组
候选探测。后者最终写出的仍只有方向可用布尔值和辅助标志；另一个调用者
`0x14195BBA0` 也只消费这些标志，没有读取任何世界空间终点。

因此，`mover+0xA70` 是随原生姿态运动逐帧更新的当前缓存，不是可以提前读取的
“下车最终 root-motion 变换”；侧向查询也不会把未来终点求解或返回给调用者。提交前
直接跳到 Free 时，原生下车动作本应逐帧累积的侧向位移没有发生，这与角色立即直立却
仍停在车体上方的现场结果一致。单独复制该缓存或复用 clearance 查询不能修复落点。

### 2026-07-26 mover 当帧运动输入边界

`DSPlayerMoverAccessor` 虚表中紧邻的一组运动接口已闭合：

- `+0x4A8` 的 `DSPlayerMoverAccessor_SetPrimaryFrameMotionInput`
  （`0x140DBB5E0`）覆盖 `mover+0x480` 的 16 字节向量；
- `+0x4B0` 的 `DSPlayerMoverAccessor_AddPrimaryFrameMotionInput`
  （`0x140DBB600`）向同一向量累加；
- `+0x4B8` 的 `DSPlayerMoverAccessor_AccumulateSecondaryFrameMotionInput`
  （`0x140DBB620`）向 `mover+0x490` 累加第二个向量，并把调用参数中的标量乘入
  `mover+0x4A0`；
- `+0x4C0/+0x4C8` 分别返回 `mover+0x490` 与 `mover+0x4A0`。

普通运动处理 `sub_140ECECA0` 在 `0x140ECEE1E` 把 `mover+0x480` 和
`mover+0x490` 都加到当前运动累加器 `mover+0x4C0`。同一轮
`DSPlayerMover_OnModifyAnimatedPose` 结束时会一次清空 `mover+0x480..+0x49F`，
并把 `mover+0x4A0` 复位为 `1.0`。因此这些槽是供其他系统提交当帧运动量的瞬时输入
通道，不是由 RideOff 预先保存、可直接读取的完整车外终点；它们本身不能回答应向哪一
侧移动多少距离。

### 2026-07-26 RideOff 位移不走 pose-motion 缓存

只读样本 `dismount_20260726_094847_447` 在原生
`DSPlayerMoverAccessor::ModifyAnimatedPose` 返回后，以同一 RideOff 会话的
`0/100/300/700/1200/2000ms` 里程碑记录 mover 状态和世界坐标。实际记录时刻分别为
`0/110/313/703/1203/2000ms`：

| 实际时刻 | animation state | pose state | Entity 世界位置 |
|---:|---:|---:|---|
| 0ms | 4 | 2 | `851.398, -4019.03, 104.36` |
| 110ms | 1 | 0 | `851.408, -4019.01, 104.366` |
| 313ms | 1 | 0 | `851.462, -4018.90, 104.36` |
| 703ms | 1 | 0 | `851.478, -4018.86, 104.346` |
| 1203ms | 1 | 0 | `851.510, -4018.77, 104.321` |
| 2000ms | 1 | 0 | `851.689, -4017.39, 102.215` |

全部里程碑中 `mover+0x5C0/+0x5C1` 均为 `0/0`，`mover+0x8D0` explicit 标志也始终
为 `0`。`mover+0x918` 与 `+0x958` 的位置在整个区间固定为
`851.398, -4019.03, 104.36`，`mover+0xA70` 也固定为
`851.455, -4018.91, 104.36`；与此同时 Entity 世界位置仍逐步变化，并在约 2 秒时
完成显著的侧向位移和落地高度变化。

因此，当前 RideOff 的正确地面位移不由 `mover+0x918/+0x958/+0xA70` 这组
pose-motion 变换逐帧提交，也不走 `+0x5C0/+0x5C1` 选择的
`DSPlayerMover_ApplyPoseMotionTransform` 分支。终结器请求 animation state `1` 后，
状态 `1` 仍通过 `0x140EC85DF` 进入普通运动处理 `sub_140ECECA0`；这条普通
mover/物理链继续消费与 `2.1021s` 长 descriptor 同步的运动，解释了状态和缓存早已
复位而角色仍需约 2 秒才落地。把任一静态 pose-motion 缓存当作未来终点的方案由此
得到运行证伪。

### 2026-07-26 RideOff 位移不来自 mover 外部当帧输入

只读样本 `dismount_20260726_100409_504` 在
`DSPlayerMoverAccessor::ModifyAnimatedPose` 原函数调用前后，以
`0/100/300/700/1200/2000ms` 六个里程碑记录普通 mover 的输入与输出；实际记录时刻
为 `0/109/312/703/1203/2000ms`。整段期间：

- 原函数调用前的 `mover+0x480` 与 `mover+0x490` 始终为全零，因此玩家下车位移
  不是由三个 accessor 当帧运动提交接口写入；
- `mover+0x440` 与 `mover+0x450` 均保持约
  `(0.422673, 0.906282, 0, 0)`，但控制
  `sub_140ED09A0` 中“方向乘速度”注入分支的 `mover+0x3D6` 始终为零，因此这组
  固定向量没有进入当前运动累加器；
- `mover+0x39C/+0x3A4/+0x5C7` 也始终为零。`+0x39C=0` 使
  `sub_140ED1D10` 的碰撞扫掠入口立即返回，`+0x5C7=0` 则跳过
  `sub_140ED11D0` 中另一组条件位移修正；
- `mover+0x4C0` 在 109ms 之后的原调用前后均为零，说明位移没有作为一个可在
  wrapper 边界截获的持续运动向量留在累加器中。

与此同时 Entity 世界位置仍从
`(851.398, -4019.03, 104.36)` 逐步变到
`(851.693, -4017.38, 102.255)`。`mover+0x500` 在每个原调用返回后记录非零的小幅
帧位移；静态代码已经确认该字段是用“本帧结束后的 Entity 位置减去调用前保存的
world-transform 快照”反算出的实际结果，而不是输入或未来终点。

因此，当前 RideOff 的约 2 秒离车位移不来自 `+0x440/+0x450` 控制向量，也不来自
`+0x480/+0x490` 外部当帧输入，且不经过上述两个条件物理修正入口。后续静态分析已经
闭合其真正输入：普通 mover 会在同一次调用内部把 `SkinnedModel` 当前帧根运动平移
短暂写入 `+0x4C0/+0x4C4/+0x4C8`，交给物理链消费后再返回。因此 wrapper 边界前后
为零只排除了“可跨边界截获的持续向量”，不再表示函数内部没有经过这组字段。
`+0x500` 仍只能用于观测已经发生的位移，不能提前提供安全落地点。

同一样本的 `capture_manifest.csv` 给出前三个截图实际中点
`117/320/719ms`。三张图中角色都仍在车辆边缘执行下车动作，和安全基线原有的约
2 秒视觉行为一致；只读字段采样没有改变动画或引入新的冻结。由于三个关键帧结论一致，
本轮按最小截图规则没有继续查看 1200/2000ms。

### 2026-07-26 Entity 位移的直接来源是 PhysicsCharacterMover

`PhysicsCharacterMoverProxy` 虚表 `+0x1B8` 的实现 `0x14247BDD0` 已命名为
`PhysicsCharacterMoverProxy_GetWorldPosition`。该函数只把 proxy
`+0x110/+0x120` 中的三个 double 世界坐标复制到调用者的 24 字节输出。

`sub_140ECECA0` 在内部移动处理 `sub_140ECBF70` 返回后，于 `0x140ECF550`
调用该虚函数；随后把所得物理 mover 坐标与 `mover+0x150/+0x154/+0x158` 的局部
偏移相加，构造玩家的新 world transform，并在 `0x140ECF703` 起持有 Entity transform
锁写入 `Entity+0xE8..+0x127`。本帧结束时 `mover+0x500` 反算出的位移正是这次
物理 mover 坐标提交造成的 Entity 差值。

同一虚表 `+0x1C0` 的 `0x14247C3D0` 已命名为
`PhysicsCharacterMoverProxy_SetWorldPosition`。它在可选的 proxy 独占锁内把调用者
提供的三个 double 坐标写入 `proxy+0x110/+0x120`，并把相同位置转发给活动的底层
character mover。由此，`+0x1C0` 写入与 `+0x1B8` 读取形成了完整的当前世界位置边界；
这两个接口都只处理当前坐标，没有终点或动作剩余位移参数。

因此，普通路径中玩家世界位置的权威当前值位于 `PhysicsCharacterMoverProxy`，而不是
动画 pose-motion 缓存或 mover 外部当帧输入。proxy `+0x110` 仍只是每帧更新后的当前
坐标，不是预先保存的下车终点；它解释了为什么输入 wrapper 边界可以全零而 Entity
仍然移动，也解释了提交前直接 detach 没有驱动物理 mover 完成侧向/落地轨迹时，角色
会保留在座位附近并站到车体上。

### 2026-07-26 PhysicsCharacterMover 的逐帧积分边界

`PhysicsCharacterMoverProxy` 虚表 `+0x1E8` 的实现 `0x14247FCC0` 已命名为
`PhysicsCharacterMoverProxy_UpdateMovement`。它在独占锁内保存
`proxy+0x110/+0x120` 的更新前世界坐标，并调用已经命名为
`PhysicsCharacterMoverProxy_IntegrateMovement` 的 `0x14247F800`。后者把
`proxy+0x170` 的当前运动向量送入最多五个碰撞解析子步，并把每个子步得到的修正位移
直接累加到 `proxy+0x110/+0x120`。

积分返回后，`UpdateMovement` 用
`(更新后坐标 - 更新前坐标) / delta` 反算实际运动速度并写回 `proxy+0x170`，
再通过活动底层 character mover 的虚表 `+0x1C0` 提交更新后的当前世界坐标。这个调用
与前述 `PhysicsCharacterMoverProxy_SetWorldPosition` 使用同一个底层当前位置写入
槽，确认 `+0x170` 是逐帧输入并被实际碰撞结果覆盖的当前运动状态，而不是预先保存的
RideOff 终点。

同一虚表 `+0x100` 的 `0x14247C350` 已命名为
`PhysicsCharacterMoverProxy_SetMovementInput`。它确实把调用者给出的 16 字节向量
写入 `proxy+0x170`；但当前普通 mover 路径在 `0x140ECC6DC` 调用该槽时传入的是明确
构造的全零向量。因此这个已闭合的调用点只会清空 proxy 运动输入，不会提供运行中观察
到的 RideOff 侧向轨迹，也不能作为即时落点来源。

因此，`+0x1E8` 本身只会从当前运动状态经过连续碰撞子步得到新的当前坐标；它没有一个
可在 Drive→Free 时直接读取或一次复制的最终地面坐标。结合已经确认的
`+0x1B8/+0x1C0` 当前坐标边界，这组接口仍不能为提交前直接 detach 补出原生 RideOff
结束位置，也不能把 `proxy+0x170` 当作安全落点使用。

### 2026-07-26 独立 PhysicsCharacterMover 配置链

此前只识别成普通字段写入的 animation accessor slot `0x4E0` 已完成闭合：
`0x140DBB710` 现命名为
`DSPlayerMoverAccessor_RequestPhysicsMoverMode`。它把一次性 pending 位写到
`mover+0x6F0`，把 mode 写到 `mover+0x6F4`。RideOff OnEnter 在非 runtime mode 3
路径请求 mode `2`；RunPresentation 也先请求 `2`，并在其条件分支中可能用 mode `1`
覆盖同一帧的待处理值。此前对 animation state `4` 和 mover request `1` 的门控没有
覆盖这个独立请求。

`DSPlayerMover_OnModifyAnimatedPose` 在 `0x140EC8334` 消费 pending 值并清除
`mover+0x6F0`；没有显式请求时，才从玩家组件的 `+0x37C` 字段归一为默认 mode
`0/1`。新 mode 与 `mover+0x6F8` 中已应用 mode 不同时，函数按下表选择全局玩家
配置块中的 16 字节 key：

| mode | configuration key 偏移 |
|---:|---:|
| 0 / 默认 | `+0x1D0` |
| 1 | `+0x1E0` |
| 2 | `+0x220` |
| 3 | `+0x1F0` |
| 4 | `+0x200` |

切换期间，函数会把 `mover+0x7E0` 的 RideOffCollector 临时挂到物理 mover
配置对象 `+0xC8`，调用后立即清空；这进一步限定了 collector 的作用是参与碰撞候选
过滤，而不是保存落点。随后调用的 `0x1402FCFD0` 已命名为
`PhysicsCharacterMoverSelector_ApplyConfigurationKey`：它把 key 解析成一个或两个
配置资源，调用 `PhysicsCharacterMoverProxy` 虚表 `+0x1F8`，成功时缓存 key 并把
mode 记录到 `mover+0x6F8`。

`PhysicsCharacterMoverProxy` 的该虚函数已定位为 `0x1424806E0` 并命名为
`PhysicsCharacterMoverProxy_SetConfigurationResources`。它在 SRW lock 下构造并
检查新的碰撞候选，更新 proxy 资源、重建 mover 状态，并可拒绝不安全的配置切换；
调用链不求值或 seek 骨骼动画。因此这条链属于玩家物理碰撞/mover profile 配置，
不是此前遗漏的骨骼下车动作入口，也不直接提供 side-dependent 世界落点。

### 2026-07-26 SkinnedModel 双缓冲根运动是普通 RideOff 位移的内部输入

玩家 Entity `+0xC8` 保存的是 `Model*`。`Model` 主虚表位于 `0x1431286E0`，
其基础实现的 `+0x90` 槽 `0x14018C9F0` 只返回全零向量；玩家使用的派生对象是
`SkinnedModel`，构造函数 `0x14027BE30` 会把主虚表替换为 `0x14312F898`。

该派生虚表已经闭合两个连续接口：

- `+0x90` 的 `0x14027D630` 已命名为
  `SkinnedModel_GetRootMotionTranslation`。它读取 `SkinnedModel+0x230` 的当前
  双缓冲索引，以 `0xB0` 为 stride，复制该缓冲 `+0xD0` 的 16 字节平移；
- `+0x98` 的 `0x14027D650` 已命名为
  `SkinnedModel_GetRootMotionRotation`。它按相同索引复制该缓冲 `+0xE0` 的
  16 字节四元数；
- `SkinnedModel` 构造时把两份 `+0xD0` 初始化为零，把两份 `+0xE0` 初始化为
  单位四元数 `(0, 0, 0, 1)`。这与两个接口分别作为平移和旋转的下游使用完全一致。

普通运动函数 `0x140ED09A0` 已命名为
`DSPlayerMover_ConsumeSkinnedModelRootMotionTranslation`。它从
`mover+0x48` 取得 Entity，再从 `Entity+0xC8` 取得 Model，并通过虚表 `+0x90`
读取当前帧根运动平移。随后它应用动作倍率和 mover 选项，以
`Entity+0x100/+0x10C/+0x118` 三组世界基向量把模型局部位移转换到世界空间，
再除以 `frameDelta` 换算为速度，短暂写入
`mover+0x4C0/+0x4C4/+0x4C8`，供同一次普通 mover/物理更新立即消费。

方向处理 `sub_140ECA560` 对同一个 `Entity+0xC8` Model 调用虚表 `+0x98`，并把返回
的根运动四元数与 Entity 当前朝向复合。因此，RideOff 约 2 秒的侧向移动和落地并非
只有骨骼视觉：`SkinnedModel` 每帧产生的根运动平移/旋转确实是普通
DSPlayerMover→PhysicsCharacterMover 链的内部运动输入。

同步和异步生产端也已交叉确认这只是“当前动画子步”结果：

- `AnimatedPosePipeline_UpdateSynchronous` 每个子步先异或切换
  `SkinnedModel+0x230`，再把新缓冲 `+0xD0` 清零、`+0xE0` 置为单位四元数，然后
  才执行 `AnimatedPosePipeline_EvaluateAndDispatchModifiers`；
- `AnimatedPosePipeline_EvaluateAsyncPhase` 执行完全相同的切换和初始化，再用
  `jobContext+0x1C` 的本子步 `frameDelta` 求值；
- `AnimatedPosePipeline_CommitAsyncPhase` 只提交 Evaluate 已生成的当前姿态、更新
  consumers 并传播模型变换；它不切换缓冲、不重新求值，也没有 seek 或末端采样。

ActionGraph/AnimationGraph 结果到这两个缓冲字段的标准写入链也已闭合：

- `GraphAnimationManager_EvaluateFrame` 从 `MsgGetAnimatedPose+0x10` 取得 pose context；
  当求值结果已经物化时，`0x14022567B` 会把结果 `+0x20/+0x30` 的两个 16 字节
  motion 字段及 `+0x40` 有效字节复制到 pose context 持有的结果对象；
- `0x140225B10` 已命名为 `AnimationGraph_MaterializeResultToPoseContext`。
  `poseContext+0x18` 指向当前
  `SkinnedModel+0xD0+0xB0*currentIndex` 根运动缓冲；若
  `graphOutput+0x40` 已提供物化结果，`0x140225EAF` 把其 `+0x30` translation
  复制到缓冲 `+0x00`，`0x140225EC1` 把其 `+0x20` rotation quaternion 复制到
  缓冲 `+0x10`；
- 这两个目标地址正是随后
  `SkinnedModel_GetRootMotionTranslation/Rotation` 从模型 `+0xD0/+0xE0`
  读取的字段。关键复制点、目标指针和函数原型均已在 IDA 中更新。

因此，Graph motion transform 确实通过原生 pose context 物化链进入玩家
SkinnedModel，再由 DSPlayerMover/PhysicsCharacterMover 消费；它不是只存在于
fullgame 的不透明结果里。不过这条标准链仍只物化本次求值区间的增量，并没有另存
动作剩余累计值。

这也解释了此前运行采样的表面矛盾：`+0x4C0` 在 accessor wrapper 前后都为零，
是因为根运动只在原函数内部短暂写入并被立即消费；它不是跨调用保存的未来端点。
上述两个 getter 均没有时间、seek、剩余位移或终点参数，只暴露刚完成求值的当前
双缓冲项；下一动画子步会先清除新缓冲再生成新的增量。因此这条链确认了正确轨迹的
逐帧来源，但没有保存一个可直接读取的累计剩余位移或最终车外落点。


## 提交前绕过的错误落点
用户现场视觉观察进一步确认：角色确实瞬间离开座位、没有再播放约 2 秒的下车动作，
但落点位于车辆上方，角色变成站在车体上，而不是站到车辆旁的安全地面。
`capture_manifest.csv` 的实际中点为 125ms、321ms、711ms；三帧中位于车体上方的
直立角色与该观察一致。因此这个提交前绕过只证明“不进入 RideOff 可以消除视觉动作”，
同时也证明在正常下车上下文中单独强制 operation 21 并直接切 Free 不会完成正确的
地面落点求解。它属于错误传送/落点失败，当时的快速下车视觉目标仍未完成；该候选必须撤回，
不得在错误落点之上继续叠加未经静态证明的位移修补。

这条失败证明确立的约束仍有效：不能把 operation 21、side `0/1/2`、当前
pose-motion 缓存、当前 physics 坐标或观察到的当帧位移单独当成最终落点。

## Graph 末端区间：空中落点与操控冻结

视觉样本 `dismount_20260726_115305_099` 没有直接写世界坐标，也没有提交前 detach。
在 RideOff 活动会话和 fullgame 返回 RVA `0x366F423` 同时匹配时，它保留时间状态
`start=0`，把 `end=0.00834168` 提交到 descriptor duration `2.1021`，只调用一次原生
evaluator。结果立即成为 `duration=2.1021 sync=2.1021 complete=1`。

该结果随后沿本章已闭合的原生物化链写入 SkinnedModel 根运动双缓冲，再由
DSPlayerMover/PhysicsCharacterMover 消费。旧分析曾根据 117ms、320ms、726ms 三张
截图把它判断成“车旁落地”，但截图视角无法给出碰撞体或 Entity 的实际高度；用户的
操控测试只确认玩家动作冻结，没有证明落点正确。

2026-08-30 的同路径复测
`artifacts/boarding/fast_dismount_20260830_121409_480` 加入了同帧 Entity／当前 proxy
坐标。严格末端姿态在 `31ms` 消费后，两者都位于 `Z=104.919`，差值与
`mover+0x150` 偏移均为零；稳定地面实际为约 `Z=102.215`。`47ms` 随即由
`callerRva=0x110694D` 请求 Fall，`406ms` 降到 `Z=102.368`，直到 `2500ms` 才到
`Z=102.215` 并进入 Basic。由此确认严格 Graph 末端本身也是空中落点；旧截图结论
错误，不能再把该候选作为正确空间落点的证据。

Graph 末端区间候选同时存在空中落点和动作生命周期冻结，已经判定失败并从游戏目录
撤回。

## 安全诊断基线仍执行原生下车姿态

样本 `dismount_20260726_184026_386` 使用未改写 Graph 时间结果的安全诊断基线。
`capture_manifest.csv` 给出的前三个实际中点为 124ms、320ms、711ms。124ms 帧中
玩家身体和双腿仍横跨车辆；320ms 与 711ms 帧中的身体、手臂和持械姿态继续变化，
仍属于下车动作过程。

因此该样本不满足“100ms 已在车外，300ms、700ms 无下车姿态”的视觉成功条件；
只看规定的三个最小关键帧已经足以判定视觉跳过失败，无需查看 1200ms 和 2000ms。
它同时确认新增只读队列／事件观测没有意外把安全基线变成快速下车实现。

后续自动样本 `dismount_20260726_190215_760` 使用同一展示快速退出／只读诊断路径。
用户直接观察后再次确认：下车后玩家动作仍然冻结，没有恢复可用操控。因此这条路径
不能再以“无冻结安全基线”表述；`FLOW_PASS`、正常菜单退出和只读观察均不改变它作为
快速下车 Mod 失败的结论。

## 2026-08-23：原生终点前交接的落地与操控验证

严格终点后手动 detach 的失败样本
`artifacts/boarding/fast_dismount_20260823_224139_933` 解释了用户观察到的“靠近地面
卡住，随后下落”。Mod 在 mover 前请求 detach 时，位置为
`851.826094,-4017.095116,102.7890029`；mover 消费末端根运动后为
`851.9099104,-4016.913833,102.8010931`。虽然 RideOff 与 RideVehicle 于 `531ms`
退出，但 Basic 没有接管；到 `937ms`，Fall 入口 `0x110694D` 命中，位置已降到
`851.2807263,-4018.275836,102.3385162`。因此旧候选不是稳定落地，只是把角色留在
地面上方的动作空档，随后由 Fall 修正。

当前实现不再在 mover 前 detach，也不强制严格末端。通过样本
`artifacts/boarding/fast_dismount_20260823_231008_715` 在 `1094ms` 记录 RideOff
OnExit、RideVehicleActionPlugin OnExit 与 Basic OnEnter；三次调用看到完全相同的
位置 `851.7032313,-4017.35567,102.2153168`，Z 值已处于稳定地面高度。整个测试窗口
没有 Fall 入口 `0x110694D`。

测试在退出后立即按住 S，并把截图窗口缩短为目标 `25/50/100/200/400ms`。实际
`148ms`（目标 100ms）帧已出现明确转身/迈步，实际 `429ms`（目标 400ms）帧已离开
原位置。这里的移动不是由残余根运动推断，而是退出后主动输入与连续画面共同验证。
由此，当前卡车驾驶位样本已经同时闭合车旁落点、Basic 接管、Fall 缺席和玩家控制恢复。

## 2026-08-30：`duration-0.1s` 加代理扫掠候选失败

人工复测确认该候选并未把玩家稳定放到地面：画面表现为角色瞬移到车前方空中，
随后自然落下。测试日志、被否决的 ASI 与当次完整日志保存在
`artifacts/boarding/manual_air_teleport_20260830_120201`。

日志直接区分了碰撞代理位置与玩家 Entity 位置：

- `11:59:37.034` 的最后一次强制扫掠把 proxy `0x258C5889D80` 保持在
  `Z=101.843`；
- `29ms` 后的原生 RideOff OnExit（`callerRva=0xF97B56`）看到的玩家 Entity
  却仍为 `Z=104.760`，不能把前一个 proxy 坐标当成同一时刻的玩家落点；
- 同一个 `109ms` 时间点随后由 `callerRva=0x110694D` 请求 animation state `3`，
  Entity 仍为 `Z=104.758`。因此此前“没有进入 Fall”的结论也是错误的；
- 到 `468ms`，Entity 才降到 `Z=102.337`；到 `2156ms` 才稳定在
  `Z=102.216` 并再次进入 Basic。这个连续变化与人工看到的空中下落完全一致。

静态链也说明了原判断为什么无效。`PhysicsCharacterMoverProxy_UpdateMovement`
`0x14247FCC0` 只在代理锁内推进 proxy `+0x110`，并把该坐标转发给底层 character
mover。玩家 Entity 的世界位置是在外层普通 mover 路径 `sub_140ECECA0` 中重新读取
当前代理坐标后，于 `0x140ECF703` 另行提交到 `Entity+0xE8`。Mod 额外直接调用一次
proxy Update 并不执行这段 Entity 提交；本次日志中也不存在预期的
`FastRideOff exit grounding physics sweep`，说明 OnExit 时的额外刷新实际没有成功。

因此，`Basic` 状态、代理接触或测试流程完成都不能替代可见 Entity 轨迹验证。
`duration-0.1s` 加 5 米代理向下扫掠候选已经判定失败并从游戏目录撤下，不能作为
稳定落地实现。

同日的严格末端加原生 `>10s` 兜底完成候选
`artifacts/boarding/fast_dismount_20260830_121409_480` 进一步排除了“只修生命周期”
的做法：Graph 完整末端、队列时钟同步和 mover 姿态消费都按顺序完成，RideOff 也在
`47ms` 进入 OnExit；但此时 Entity 与当前 proxy 同为 `Z=104.919`，随后立即进入
Fall，`2500ms` 才落地并由 Basic 接管。严格末端并不包含自然下车期间逐帧物理／重力
累积出来的稳定地面结果。

## 2026-08-30：终点姿态后的同帧原生碰撞提交

样本 `artifacts/boarding/fast_dismount_20260830_123237_847` 不再等待异步 Graph 输出
报告 `reachedEnd`。该回报在此前运行中曾于 `31ms` 出现，也曾延迟到 `266ms`；等待它
会让已经位于车外空中的玩家持续可见。新路径在目标 endpoint 已认领且队列时钟同步后，
让首个终点姿态先经过原生 mover，再使用同一活动
`PhysicsCharacterMoverProxy_UpdateMovement` 的参数执行 5 米垂直向下输入。只有实际
下降距离小于 5 米、即原生碰撞确实截断扫掠时，才把 proxy 的截断坐标按原生
`Entity+0x2A8` 锁和 `Entity+0x98` dirty-bit 顺序提交给玩家 Entity，并开放 RideOff
原生 `>10s` 完成分支。

本次同帧快照记录到：

- `16ms` 的终点 mover 前，Entity 与活动 proxy 均为 `Z=104.3602708`；
- 同一次终点根运动消费后，两者均为 `Z=104.9193905`，确认该姿态本身仍在空中；
- 原生碰撞下探把两者同时截断到 `Z=102.2153172`，Entity／proxy gap 与 mover offset
  均为零。这个高度与此前自然落地的约 `Z=102.215` 一致，而不是被人工复测否决的
  proxy-only `Z=101.843`；
- 下一次 RideOff Update 于 `31ms` 走原生完成分支，OnExit 与 Basic OnEnter 看到的
  Entity／proxy 仍同为 `Z=102.2153172`；控制窗口没有出现 Fall 入口
  `callerRva=0x110694D`。

该样本证明了“终点根运动后、退出状态前”可以在一个 mover 帧内闭合同一个活动
proxy 的原生碰撞结果与可见 Entity 坐标，修正了此前把 proxy 接触误当成玩家落地的
错误。不过自动截图的 `47ms` 至 `227ms` 画面被车体遮挡，不能仅凭这组截图断言玩家
视觉姿态已经符合最终需求；最终视觉效果仍须与人工观察区分记录。
