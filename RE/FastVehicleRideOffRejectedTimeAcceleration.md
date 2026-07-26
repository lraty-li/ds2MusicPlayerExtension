# 快速下车：已证伪的时间加速方案

日期：2026-07-26

返回[当前状态与知识索引](FastVehicleBoardingModImplementation.md)。

> **结论：**所有通过放大 delta、缩放长 descriptor、重复 Graph 或重复姿态求值来
> “跑完”约 2 秒 RideOff 的方案均已运行证伪并撤回。它们不是当前候选，不得恢复。

## 证伪总表

| 方案 | 已确认结果 |
|---|---|
| 仅放大 `DSPlayerMover` frameDelta | 视觉动作仍约持续 2 秒 |
| RideOff 长 descriptor `timeScale=512` | 没有得到即时且正确的车外落地 |
| 单次大 `GraphAnimationManager` delta | 没有缩短视觉动作 |
| 同一帧重复 Graph 求值 | 骨骼到末段，但 root motion 未逐步消费，角色冻结在车辆上 |
| 重复 `AnimatedPosePipeline` 内层 | 早期到车外，随后冻结在迈步/屈腿姿态 |
| 重复同步外层 | 实际 RideOff 不走同步更新路径，wrapper 未命中 |
| 72 对异步 Evaluate/Commit | 336ms 与 719ms 均冻结于相同屈腿姿态 |

## Graph 单独推进
`GraphAnimationManager_EvaluateFrame`（`0x140225470`）单独放大一次 delta 没有缩短
视觉动作。重复 96 次 `1/30s` 求值会把骨骼推进到末段姿态，却没有逐步消费 root
motion，结果是角色以站立姿态冻结在车辆上；该实验钩子已移除，部署版本已恢复无冻结
回归的基线。

## 姿态管线重复
静态分析确认完整原生管线为
`AnimatedPosePipeline_EvaluateAndDispatchModifiers`（`0x14027D670`）：

```text
MsgGetAnimatedPose
  -> MsgPostAnimationUpdate
  -> MsgPreModifyAnimatedPose
  -> MsgModifyAnimatedPose
  -> DSPlayerMover 应用本子步 root motion
```

同一 `frameDelta` 被写入图求值和姿态修改消息，管线对象 `+0x48` 为 Entity，可与当前
RideOff 的 player 精确匹配。重复执行该“内层”管线 72 次 `1/30s` 后，root motion
已被逐步消费，117ms 关键帧中角色确实位于车外；但 320ms 起角色冻结在迈步/屈腿姿态，
直到 2007ms 仍未恢复。原因边界已收窄到该函数不包含调用者负责的姿态双缓冲切换和
提交收尾。这个候选仍属视觉失败，已从实现和部署版本撤回。

直接调用者 `AnimatedPosePipeline_UpdateSynchronous`（`0x14027D570`）的静态流程已经
确认：每次更新先切换 `pipeline+0x230` 的双缓冲索引并初始化新缓冲，再调用上述内层
管线，随后提交当前姿态、传播模型/挂点变换并完成缓冲收尾。因此这些步骤属于一个原生
动画子步的必要边界，不能只重复内层函数。运行验证同时确认玩家下车采用分阶段更新
路径：同步函数 `0x14027D570` 在 RideOff 期间没有被调用，对它的包装未命中并已撤回；
真实路径由 `AnimatedPosePipeline_EvaluateAsyncPhase`（`0x14028A5A0`）执行求值阶段，
提交阶段由 `AnimatedPosePipeline_CommitAsyncPhase`（`0x14028A520`）另行调度。首次
成对包装运行的启动日志明确显示 wrapper 安装失败：提交阶段的短签名不是唯一匹配，
所以候选没有执行，脚本因缺少快进证据而失败并清理进程；该轮没有新的视觉结论。
换用 IDA 生成的完整唯一签名后，72 对异步“求值→提交”已实际命中；然而
`dismount_20260725_223249_456` 的 336ms 和 719ms 关键帧均显示角色以同一屈腿姿态
停在车辆结构上。由此确认即使姿态双缓冲、root motion 和模型传播逐子步闭环，同一
引擎帧内仍缺少驱动动作退出的外部状态/事件更新。该候选已撤回；下车不得再采用 delta
放大、单阶段重复或成对姿态阶段重复等时间快进方案。

## 约束

上述结果共同证明，动作图时间、姿态双缓冲、root motion 消费、物理积分和驱动动作退出的
外部状态并不是一个可在同一引擎帧内靠重复调用安全闭合的单层时钟。后续实现不得重新
叠加这些方案，也不得以“流程完成”代替里程碑截图中的视觉完成。
