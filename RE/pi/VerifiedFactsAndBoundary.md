# 已验证事实和当前边界

## 三个上车方向

| 方向 | rideKind | 表现 |
|------|---------|------|
| 正前方 | 0 | 瞬间上车 |
| 左前方 | 1 | 长时间上车动画 |
| 右前方 | 2 | 长时间上车动画 |

## v0.11.0 当前方案

在 `RideOnState_OnEnter` 钩子中：

1. 调用原始 OnEnter（设置参数包络和动画状态）
2. 从 RideOnState vtable slot [27] (+0xD8) 读取 `ProcessVehicleAttach` 并提前调用
   - 使 stage 0→1，实体提前附着，座位过渡提前开始
3. 覆写 `runtime+0x2A0`（rideKind）为 0（正前方）
   - 使左侧/右侧上车使用正前方的瞬间上车动作

### 关键函数

| 地址 | 名称 | 签名 |
|------|------|------|
| 0x140F98D00 | `DSPlayerVehicleRideOnState_OnEnter` | `48 8B C4 48 89 58 ? 48 89 70 ? 57 41 56 41 57 48 81 EC ? ? ? ? C5 F8 29 70 ? C5 F8 29 78 ? C5 78 29 40 ? 45 33 FF` |
| 0x140F99C60 | `DSPlayerVehicleRideOnState_Update` | `40 53 56 57 48 83 EC ? C5 F2 58 81` |
| 0x140F9A390 | `DSPlayerVehicleRideOnState_ProcessVehicleAttach` | 从 vtable slot [27] 获取 |

### 逻辑结构

```
rideOn+0x88 → plugin (RideVehicleRuntimePlugin)
rideOn+0x190 → runtime
rideOn+0xB0 → anim component
runtime+0x2A0 → rideKind (approach direction)
runtime+0x220 → seat reference (resolves via sub_1401783C0 to seat entity)
plugin+0x11A → next state byte (2 = Drive)
```
