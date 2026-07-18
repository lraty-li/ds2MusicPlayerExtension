# 玩家快速上车 Mod 实现与验证

日期：2026-07-18

## 结果

`ds2_vehicle_boarding_trace` 当前构建已经实现玩家快速上车。左前方自动测试中：

- 保留原生 RideOn OnEnter、实体挂接、seat transition 与 stage 0→1→2；
- 上车角色 descriptor 由原 evaluator 在首个活动帧直接求值到末端；
- `DSCutInCamera` 由原 slot 9 自己设置 finished；
- 下一帧由 CameraMode 调用原 slot 5 `Deactivate`，完成 target、observer 和 runtime
  entry 清理；
- CutIn 清理后的后续 RideOn Update 才放行 `0xED`，进入原生公共完成块；
- `Drive Enter` 在 RideOn elapsed 约 `0.063s` 时完成；
- 最早画面已经是车辆后方驾驶镜头，没有车侧攀爬或残留 CutIn；
- 原生下车与退出流程保持正常。

实现没有修改寄存器、可执行代码字节或固定 RVA。所有类级入口通过 RTTI/COL 定位，
fullgame 入口通过两个唯一模式解析并校验为同一可写 evaluator 指针槽。

## 会话边界

`FastBoardingSession` 由 RideOn vtable wrapper 建立：

1. slot 11 原生 OnEnter 返回后记录 RideOn、plugin、玩家 ActionParams 和
   `GraphAnimationManager`；
2. slot 27 原生 `ProcessVehicleAttach` 返回后，只在
   `current=1,next=1,stage=2,b189=1,b18A=1,b191=1` 时进入 ready；
3. RideOn Update slot 14 用线程局部范围限定 Graph bool-event wrapper；
4. Graph 完成前再次验证 `current=1,next=1,stage=2,b18B=1`；
5. Drive Enter 只接受同一 plugin 的状态边界。

角色 descriptor、CutIn 实例/action hash 和 Graph 事件都绑定在同一个有界会话内。
任一必需组件定位失败时，required-component mask 不成立，功能 wrapper 只透传原函数。

## 角色动画的正常 C++ 介入点

fullgame 间接 evaluator 的完整原型为：

```cpp
void EvaluateDescriptor(
    ActionGraphResult* output,
    Descriptor* descriptor,
    uint8_t mode,
    float timeScale,
    bool evaluateExtraChannels);
```

主上车调用原生参数为 `(output, descriptor, 0, 1.0, true)`。DS2 evaluator 核心
确认第 4 参数是时间缩放：它缩放 descriptor 采样区间，并把输出 duration 写成
`descriptorDuration / timeScale`；第 5 参数控制额外姿态/结果通道，必须原样透传。

功能 wrapper 只在唯一的 `0x0BC4A758` 活动上车 leaf、当前 RideOn ready、首次求值时
把 `timeScale` 从 `1.0` 改为 `512.0`。原 evaluator 仍负责 count、items、single、
引用所有权、sync 和结束标志，不构造空结果，也不写内部播放头。

最终测试日志：

```text
scale=1->512 mode=0 pose=1
duration=0.00694053 sync=0.00694053 end=1 complete=1
```

这取代了旧的 `ActionGraphResult_PropagateSyncFrame` JumpHook。静态分析还确认，对同一
Result 自传播虽然内存安全，但因 input/output frame 相同会直接返回，语义上不能加速。

## CutIn 的正常完成与退栈

`DSCutInCamera` 通过精确 RTTI、primary COL offset 0 和 vtable slot 9 定位。slot 9
原型为 `void(self, float frameDeltaSeconds)`。普通 CutIn 的主 elapsed 使用相机上下文
内部 delta，而不是传入的 float；因此实现没有伪造参数或写 finished 字段。

wrapper 先正常调用一次，再在以下条件成立时重复调用原 slot 9：

- 当前 RideOn 会话 ready；
- action hash 属于已静态闭合的十六个上车请求 hash；
- active、未 finished、variant 稳定；
- 没有末帧保持或 variant-advance flags；
- 每次原调用后 elapsed 严格前进，hash、variant、duration 均不变；
- 更新次数受硬上限约束。

最终左前方测试中，原 slot 9 被调用 389 次，原函数将：

```text
elapsed 0.00834168 -> 3.25327
duration 3.25325
finished=1
```

实现不在这里放行 RideOn。下一帧 CameraMode 的 slot 3 返回 false 后，原生调用
slot 5 `DSCutInCamera_Deactivate`。wrapper 在原 Deactivate 返回后验证：

- active/finished 均清零；
- selected variant 清空；
- current hash 恢复为无效值；
- flags 与 switch pending 清零。

IDA 同时确认原 slot 5 已释放 broker vehicle target、related observers，并清空
runtime entries。只有这个清理点的下一 tick 才把 CutIn 层标记为完成。

## RideOn 原生完成

`GraphAnimationManager` 通过 primary RTTI/COL offset 0 定位，slot 28 原型为：

```cpp
bool QueryBoolEvent(
    GraphAnimationManager* manager,
    uint32_t mappedEventId,
    int32_t contextIndex);
```

RideOn 的外部 `0xED` 映射为内部 ID `186`，context 固定为 0。wrapper 只在同一玩家
manager、RideOn Update TLS 范围和合法 stage 2 快照中协调该查询。若角色 descriptor
已完成但 CutIn 尚未 Deactivate，原生 true 会被暂存；CutIn 清理后的后续查询返回
true，原 RideOn Update 随即执行完整公共完成块并请求 `next=2`。

最终日志顺序为：

```text
descriptor complete                         elapsed≈0.029s
CutIn slot9 finished
mount-arrival b18B 0->1                    elapsed≈0.029s
CutIn Deactivate clean=1
Graph internal event 186 released
Drive Enter                                elapsed≈0.063s
```

Drive Enter 返回后 `b18B=1,b191=1,b381=0x4`。

## 自动化验证

代码变化后使用 `ds2_vehicle_boarding_trace/build.ps1` 构建，最终结果为 0 warning、
0 error。根目录 `test_boarding.ps1` 连续两轮均完整通过：

```text
PASS: fast boarding, Drive, dismount, and quit confirmed
```

最终截图 `drive_0200ms.png`、`drive_0700ms.png`、`drive_1700ms.png` 均显示车辆后方
驾驶镜头与驾驶位角色，没有原上车攀爬序列；`dismount1_settled.png` 显示玩家正常
站在车辆旁。日志以正常 `DLL_PROCESS_DETACH` 结束，没有 CrashTrace。

本轮运行验证范围是当前游戏版本的左前方上车路径；文档不把未运行的其他车辆与
approach 组合记作已验证结果。
