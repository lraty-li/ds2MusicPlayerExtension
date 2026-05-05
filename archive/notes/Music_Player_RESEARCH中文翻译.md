以下是忠于原文的中文翻译： 

---

# DS2 音乐播放器音频 —— 逆向工程笔记

这些笔记来自我对《死亡搁浅 2》音乐播放器音频系统的摸索与逆向分析。
目标是添加可播放自定义音频的自定义曲目。

**注入这一侧已经成功。** 我已经让全新的曲目槽位出现在实际的音乐播放器 UI 中，并带有自定义标题和艺术家文本，与原版曲目并列显示。两个自定义曲目都可以被选中、显示，而且音乐播放器 UI 会把它们当作真实条目处理。据我所知，这是第一次有人把新的 `DSMusicPlayerTrackResource` 条目添加进游戏内音乐播放器。

音频播放这一侧还没有完成。槽位已经有了，但按下 Y 时仍然播放源曲目的音频（也就是我克隆所依据的那个现有曲目）。分享出来，希望能帮到下一个接手的人。

测试基于 Steam 版 DS2，manifest 为 `3400946842679455339`
（截至 2026 年 4 月为当前版本）。镜像基址为 `0x140000000`。这里提到的具体函数偏移都来自该版本，更新后会漂移。

## 环境设置

`DS2.exe` 是一个很小的启动器桩程序（约 1.2 MB）。实际游戏会作为第二个更大的模块（>80 MB）再次映射进同一进程。除非另有说明，下文所有地址都相对于这个大模块的基址。运行时可以通过枚举已加载模块，并选出 `SizeOfImage > 0x5000000` 的那个来定位它。

`DS2.exe` 静态链接了 Wwise，并导出了 `AK::*` 符号。可以对这些修饰名调用 `GetProcAddress` 获取真实函数指针，例如：

```cpp
?PostEvent@SoundEngine@AK@@YAII_KIP6AXW4AkCallbackType@@PEAUAkCallbackInfo@@@ZPEAXIPEAUAkExternalSourceInfo@@I@Z
```

## 音乐播放器对象图

我关心的 Decima RTTI 类型如下：

```cpp
DSMusicPlayerSystemResource
  +0x20: AllArtists  (RawArray of DSMusicPlayerArtistResource*)
  +0x30: AllTracks   (RawArray of DSMusicPlayerTrackResource*)

DSMusicPlayerTrackResource (size 0x300)
  +0x20: TrackId           (uint32)
  +0x24: Seconds           (uint16, displayed duration)
  +0x26: MenuDisplayPriority (int16) -- NOT actually used by UI sort
  +0x28: Flag              (uint8)
  +0x30: AlbumResource     (Ref<AlbumResource>)
  +0x38: TitleText         (Ref<LocalizedTextResource>)
  +0x40: SoundResource     (Ref<SoundResource>) -- full play
  +0x48: TrialSoundResource (Ref<SoundResource>) -- sample preview
  +0x50: JacketUITexture   (StreamingRef<UITexture>)
  +0x58: OpenConditionFact (Ref<BooleanFact>)

DSMusicPlayerAlbumResource
  +0x28: TitleText           (Ref<LocalizedTextResource>)
  +0x30: ArtistNameText      (Ref<LocalizedTextResource>)
  +0x40: ArtistNameTextForTelop (Ref<LocalizedTextResource>) -- shown in HUD overlay
```

向播放器中添加曲目的方法，是在 `DSMusicPlayerSystemResource` 完成加载后，把一个 `DSMusicPlayerTrackResource*` 追加到 `AllTracks`。我通过 hook `IStreamingSystem::Events::OnFinishLoadGroup` 来捕捉这个加载时机。

让 UI 接受这些新条目的关键在于：克隆对象必须从现有曲目**原样逐字节复制 0x300 字节**（vtable 很重要，引用计数很重要，内部 padding 也很重要）。如果可能，尽量使用 Decima 分配器来分配；实践中 `HeapAlloc` 似乎也能用，但我没有做压力测试。

UI 是按 `AlbumResource` 引用来分组曲目的，而不是按 `MenuDisplayPriority`
（尽管这个字段名看起来像是用来排序的）。把优先级设成 30000 没有任何可见效果。若要强制把自定义曲目放到特定位置，你大概率需要给它分配一个独立的 `AlbumResource`，并让那个资源在你想要的位置排序。

## 音频链（我已经确认的部分）

当用户在音乐播放器中对某个曲目按下 Y 时，引擎会发送一个 Wwise 事件。调用链如下：

```cpp
DSMusicPlayerTrackResource
  -> SoundResource (at +0x48 for sample, +0x40 for full)
    -> [resolution step i don't fully understand]
      -> LocalizedSimpleSoundResource
        -> AK::SoundEngine::PostEvent(eventId from LSSR+0xD8, gameObj, ...)
```

播放事件 ID 位于 `LocalizedSimpleSoundResource` 的偏移 `0xD8`。播放器中的每首曲目都有自己对应的 LSSR，因此也有自己的事件 ID。
（LSSR 的 `+0x6c` 是一个浮点数 `1.0f`，几乎肯定是音量缩放值，而不是事件 ID。我之前在这一点上搞错过一段时间。）

停止事件则是通用的 Wwise 事件 `0x61605A9D`（1633704605）。所有曲目共用它。

## Wwise 包装层调用栈（当前构建偏移）

```cpp
sub_1426b5ef0   game-side PostEvent wrapper (0x3D7 bytes)
                  - inserts a per-event tracking entry
                  - calls AK::SoundEngine::PostEvent with:
                      flags |= 0x100001
                      callback = sub_1426b5080  (always)
                      cookie   = pulled from the tracker entry

sub_1426a6000   thin wrapper that hard-codes some flags     (PLAY path)
sub_1426a6050   wrapper variant 2 with arg7 fconvert        (STOP path)
sub_1426a60d0   wrapper variant 3 with extra setup
sub_14269b270   stateful wrapper used by SoundInstance methods

sub_1426b5080   PostEvent completion callback. Handles
                  AK_EndOfEvent (cleanup tracker)
                  AK_Marker     (matches "MusicEnd" / "MusicTelop" strings,
                                 sets flags 0x1000 / 1 on the instance)
```

回调 `sub_1426b5080` 是识别某个 `PostEvent` 是否来自音乐播放器的最可靠信号。所有音乐事件都会使用它。

## WwiseSimpleSoundInstance

这是音乐播放器为每个曲目分配的音频实例。总大小 0x340 字节。
vtable 静态位于 `0x143440FA0`。

一些有用的实例字段：

```cpp
+0x00   vtable (== WwiseSimpleSoundInstance::vftable for SoundInstance)
+0x30   flag byte (bit0 = playing, bit1 = ?)
+0x66   reentrancy guard
+0x178  pointer to AudioNodeHolder (Decima Ref<AudioNode>)
+0x32C  current playing ID (set by Play, cleared by Stop)
+0x334  PER-INSTANCE EVENT OVERRIDE (default 0; if non-zero, Play uses this instead
        of the default audio_node[+0xD8])
```

工厂函数是 `sub_14269A710` 和 `sub_14028DB70`。它们并不是被直接调用，而是存放在 Decima 类型描述符表中（例如 `0x143440F20`、`0x143453DE8`、`0x1431251C0`），由 Decima 类型系统去触发调用。

vtable 中值得注意的条目：

```cpp
[0x40]  sub_142696410   IsPlaying  (1-line: returns *(arg1+0x187) & 1)
[0x90]  sub_142686C80   UpdatePosition
[0xF0]  sub_14269A5D0   HasOverride  (3-line: returns *(arg1+0x334) != 0)  <-- KEY
[0xF8]  sub_142685FF0   Init/RegisterGameObj/SetPosition
[0x108] sub_14269B3D0   Play
[0x110] sub_14269B4E0   Stop, 30 ms fade
[0x118] sub_14269B680   Stop variant
[0x120] sub_14269B7C0   Stop wrapping sub_14269B6B0 (uses ExecuteActionOnEvent)
[0x128] sub_14269B7E0   Play, spatial setup
[0x130] sub_14269B540   Stop, 100 ms fade
[0x138] sub_14269B5A0   Generic stop (caller-supplied fade)
```

`+0x334` 这个 override 是最干净的劫持点，**前提是**你能识别出哪个实例对应你的自定义曲目。我没能做到，因为（见下文）我克隆出来的曲目和源曲目共享同一个 LSSR。

## 值得了解的全局变量

```cpp
data_1462591F8   pointer to DSMusicPlayerSystem singleton
                   +0x47C8 = collection that the WSSI factory adds new instances to
                   +0x49E8 = something the AK_Marker callback writes into

data_146259260   pointer to the audio resource manager
                   vtable[0x20] is the resolver that binds a SoundResource into an
                   AudioNodeHolder on a fresh WwiseSimpleSoundInstance. I didn't
                   disassemble this in depth. UNKNOWN whether it materializes a new
                   LSSR or looks one up by GUID.
```

这两个全局变量在加载时都是 0。它们会在运行时由音频系统初始化阶段完成赋值。整个二进制中，`data_146259260` 被读取了 499 次，`data_1462591F8` 被读取了 37 次。

## 已经成功的部分

* **向音乐播放器 UI 添加新的曲目槽位。** 克隆 `DSMusicPlayerTrackResource`（完整复制 0x300 字节）并追加到 `AllTracks`，就能让新条目可见、可选，并被 UI 视作真实曲目。封面图、标题文本、艺术家标签以及专辑分组都会正常渲染。这部分据我所知此前还没人真正搞定。
* 通过克隆 `LocalizedTextResource` 写入自定义标题/艺术家文本。
* 通过 `AK::SoundEngine::LoadBankMemoryCopy` 在运行时加载自定义 Wwise bank。
  我使用的是一个“扩展” bank：先从内存中复制一个真实的音频 bank，找到它的 HIRC 块，再追加自定义的 Sound/Action/Event 项，使其继承该 bank 已经可用的 bus 路由。加载过程干净，返回 `AK_Success`。
* 使用 `AK::SoundEngine::SetMedia` 提供自定义 source ID 对应的 PCM 音频数据。
  返回 `AK_Success`。
* 使用我自定义的事件 ID 调用 `AK::SoundEngine::PostEvent` 会返回非零的 playing ID，这说明 Wwise 识别了这条事件链。
* MinHook 集成成功（此前我自己写的 14 字节 JMP trampoline 经常被函数序言里的相对分支搞坏）。

## 没成功的部分

* `MenuDisplayPriority` 并不能控制排序。UI 实际上按专辑引用分组。
* Hook `AK::SoundEngine::ExecuteActionOnEvent` 没成功。这个函数在序言大约第 0x14 字节处有条件跳转，我的安装一直不稳定。即使用了 MinHook，hook 上去之后也会在帧中途崩溃。后来改为 hook 上面提到的四个调用侧 wrapper，而事实证明它们本来也只会发送 Stop / Pause 动作，不负责 Play。
* 在我的 `PostEvent` hook 中替换事件 ID。替换本身触发得很干净（日志可见，也返回了有效的 playing ID），但听不到任何区别。我还没来得及完全缩小问题范围，目前怀疑有两个方向：

  * 我在扩展 bank 里添加的自定义 Sound 项其实是静音的（可能是 bus 路由、`DirectParentID` 继承，或编码格式不匹配）。
  * 我克隆出来的曲目和源曲目共享同一个 LSSR，所以即使音乐播放器触发了播放，它用的仍然是源曲目的 gameObj / event，而我 hook 到的其实是错误的调用。

## 未解问题

这些是下一步最值得搞清楚的点。

1. **`SoundResource -> LocalizedSimpleSoundResource` 的解析过程。**
   `TrackResource[+0x48]` 是一个 `SoundResource`（vtable 位于 `0x143444xxx` 区域）。工厂函数拿到的却是一个 `LocalizedSimpleSoundResource`（vtable 位于 `0x1431251A0`）。在它们之间一定有某种解析过程，几乎可以肯定是通过 `data_146259260.vtable[0x20]` 完成的。我没有深入反汇编这个 resolver。一旦搞清楚它的工作方式，你就可以为每个自定义曲目克隆一个带唯一 GUID 的 LSSR，或者直接 hook 这个 resolver，用你自己的对象替换进去。

2. **为什么我扩展 bank 里的自定义 Sound 项听起来是静音的。**
   我的 Sound 项是直接从捕获到的音频 bank 中逐字节拷贝的（因此 bus 引用和 parent ID 都与该 bank 的实际结构匹配），然后我替换了 `ulID`（`+0x00`）、`sourceID`（`+0x09`）和 `streamType`（`+0x0D`）。我尝试过 Vorbis（模板默认）和 PCM（`0x00010001`）两种写到 `+0x04` 的 codec，结果都没有任何可听输出。`SetMedia` 喂进去的是 RIFF WAV；也许实际上需要真正的 Wwise WEM Vorbis。也可能还存在某个逐 sound 的 `NodeBaseParam`，把这个 sound 绑定到了某个在我们扩展 bank 上下文中不存在的 bus。

3. **音乐播放器是否真的为我克隆出的曲目创建了独立的 WSSI 实例。**
   工厂日志里只出现了大约 17 个 LSSR（和可见原版曲目数一致）。我克隆曲目的 trial sound 指针从来没有出现。也许播放器因为我克隆的 trial sound 校验失败，根本没去创建实例；也可能它只是按可见行做懒创建，而我的行从未真正加载。

4. **音乐播放器把“当前选中的曲目索引”存在哪里（如果有的话）。**
   如果能找到它，hook 就能知道哪个 `TrackResource` 当前是“激活”的，而不必做对象识别。

## 值得优先布置的 Hook

如果你打算接着做，建议先把下面这些 hook 装起来。使用 MinHook（我自己直接打 14 字节 JMP 的办法，在函数序言里遇到条件跳转时一直炸）。

| 项目                 | 地址（当前构建）      | 用途                |
| ------------------ | ------------- | ----------------- |
| AK::PostEvent (ID) | 按导出名获取        | 捕捉所有已发送的 Wwise 事件 |
| WSSI factory A     | `0x14269A710` | 跟踪 LSSR 到实例的对应关系  |
| WSSI factory B     | `0x14028DB70` | 同上，备用构造路径         |
| WSSI Play (vtable) | `0x14269B3D0` | 查看播放时的实例状态        |
| LoadBankMemoryView | 按导出名获取        | 捕获 bank 以供分析      |

WSSI Play hook 对追踪很有用，但它只会在整首曲目的播放路径上触发，不会在 sample preview 上触发。Sample preview 会走任务队列（`sub_1426A30E0` / `sub_1426A3620`），最终调用 `sub_1426A604B`（位于 `sub_1426A6000` 内部）来执行 play。停止路径则会经过 `sub_1426A60C7`（位于 `sub_1426A6050` 内部）。

## 一个很有用的诊断方法

我加了一个轮询线程，监视 `GetAsyncKeyState(VK_Y)`，一旦检测到按键从未按下转为按下，就开启一个 3 秒窗口。在这 3 秒内，我的 `PostEvent` hook 会转储完整参数，并通过 `RtlCaptureStackBackTrace` 记录 12 层调用栈。游戏模块内部的栈帧会打印成 `game+0xOFFSET` 的形式，这样你就能直接粘贴到反汇编器里。靠这个我得到了 sample play 的调用链：

```cpp
sub_1426B6146  PostEvent call site
sub_1426A604B  inside sub_1426A6000 (PLAY wrapper)
[game-internal callback frames]
sub_1423E9032  generic dispatcher (182 callers - red herring)
sub_1426A3324  inside sub_1426A30E0 (task queue processor)
sub_1426A378E  inside sub_1426A3620
sub_14269 73E1 inside sub_142697390
... (up to the system tick)
```

我最初把窗口设成 500 ms，太短了。至少要提高到 1.5 秒；播放事件会明显晚于按键触发，因为它是通过音频任务处理器排队执行的。

## 结语

带有 flag `0x100001` 且回调为 `sub_1426B5080` 的 `PostEvent`，就是通用的“音乐播放器 sample”特征。真正的事件 ID 是按曲目区分的。按一次 Y 会触发多个事件（很可能每个 layer / voice 各一个）。停止事件永远是 `0x61605A9D`。

把这些分享出来，希望下一个接手的人能走得比我更远。`TrackResource` 克隆这一侧已经很稳。音频路由这一侧则需要一个比我更懂 Wwise bank 格式的人来补完。如果你真的搞定了，请把发现公开发到某个地方，别让这些信息再次失传。
