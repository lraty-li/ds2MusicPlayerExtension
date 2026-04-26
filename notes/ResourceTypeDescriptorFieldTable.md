# 注册函数、类型描述、字段表的还原逻辑

本文记录资源反射信息的还原方法。结论只来自 IDA MCP 读取到的
反编译、字符串引用、数据引用和静态数据内容。

## 1. 从注册函数找到类型描述

资源类型通常有一个注册函数，函数中会调用 `sub_7FF728836920`。
这个调用把类型名字符串和类型描述地址一起交给反射/符号系统。

以 `SoundResource` 为例：

```c
sub_7FF728836920(
    a1,
    2,
    &word_7FF72E7DAB10,
    "SoundResource",
    "SoundResource",
    0,
    0,
    0,
    0,
    1,
    0);
```

这里的关联依据是同一个注册调用同时出现：

- 类型描述地址：`word_7FF72E7DAB10`
- 类型名：`SoundResource`
- 符号名：`SoundResource`

因此 `word_7FF72E7DAB10` 可以作为 `SoundResource` 的类型描述。

`DSMusicPlayerTrackResource` 同理：

```c
sub_7FF728836920(
    a1,
    2,
    &unk_7FF72CABA950,
    "DSMusicPlayerTrackResource",
    "DSMusicPlayerTrackResource",
    0,
    0,
    0,
    0,
    1,
    0);
```

因此 `unk_7FF72CABA950` 是 `DSMusicPlayerTrackResource` 的类型描述。

## 2. 类型描述中的固定字段

类型描述开头包含若干固定元数据。当前已确认对结构还原有用的是：

- 对象大小
- 对齐或基础单位
- 构造函数指针
- 析构函数指针
- 类型名/父类或关联描述指针
- 字段表指针
- 注册辅助函数指针

以 `SoundResource` 的类型描述为例：

```text
word_7FF72E7DAB10:
  size        = 0xD0
  align       = 0x10
  ctor        = sub_7FF72AE19F70
  dtor        = sub_7FF72AE19F80
  field_table = 0x7FF72C90EAE0
  register    = sub_7FF72AE19FD0
```

这里 `size = 0xD0` 也能和构造函数互相印证：
`sub_7FF72AE1A110` 初始化到 `a1 + 0xC8`，最后字段继续占用到
`0xCB`，结构按描述大小结束于 `0xD0`。

`DSMusicPlayerTrackResource` 的类型描述同样给出：

```text
unk_7FF72CABA950:
  size        = 0x60
  align       = 0x08
  ctor        = sub_7FF7293A7530
  dtor        = sub_7FF7293A7570
  field_table = 0x7FF72C50D020
  register    = sub_7FF7293A7590
```

## 3. 字段表项的读法

字段表由连续字段描述项组成。每项固定步长为 `0x38` 字节。
在当前样本里，字段项中稳定可读的关键位置是：

```c
struct FieldDescLike {
    void *type_desc;       // +0x00: 字段类型描述
    uint64_t offset_code;  // +0x08: 字段偏移，可能带标志位
    char *field_name;      // +0x10: 字段名字符串
    uint64_t reserved_18;  // +0x18
    uint64_t reserved_20;  // +0x20
    uint64_t reserved_28;  // +0x28
    uint64_t reserved_30;  // +0x30
};
```

普通字段的 `offset_code` 低位就是字段偏移。例如：

```text
type_desc   = 0x7FF72C2D37E0
offset_code = 0x0000000000000064
field_name  = "MinDist"
```

表示 `MinDist` 位于 `0x64`。

部分字段的 `offset_code` 带高位标志。例如：

```text
DSMusicPlayerTrackResource.Flag:
  offset_code = 0x10028
```

低位偏移是 `0x28`，高位标志的精确含义需要继续分析字段解析函数，
不能只凭字段表直接下结论。

## 4. 字段类型名的来源

字段表项的 `type_desc` 指向另一个类型描述。继续读取这个描述，可以得到
字段类型线索。常见例子：

```text
0x7FF72C2D58E0 -> "float"
0x7FF72C2D4F58 -> "bool"
0x7FF72C2D5650 -> "uint8"
0x7FF72C2D4020 -> "uint32"
0x7FF72C9119A0 -> "Ref_SoundGroup"
0x7FF72C913A60 -> "LinearGainFloat"
0x7FF72C90F6E0 -> "ESoundInstanceLimitMode"
0x7FF72E7DB060 -> "SoundShape"
```

这些类型名可以作为字段类型线索，但不等于最终 C++ 类型已经完全确定。
例如 `LinearGainFloat` 可以说明字段语义和存储倾向，但仍要结合构造函数、
访问代码和析构逻辑判断它在 C/C++ 结构中的实际表示。

## 5. 用构造和析构校验字段表

字段表提供字段名、偏移和类型线索；构造/析构负责校验字段宽度和生命周期。

`DSMusicPlayerTrackResource` 的字段表显示：

```text
0x30 AlbumResource
0x38 TitleText
0x40 SoundResource
0x48 TrialSoundResource
0x50 JacketUITexture
0x58 OpenConditionFact
```

析构函数 `sub_7FF72939F620` 对 `0x30`、`0x38`、`0x40`、`0x48`、
`0x58` 调用引用释放函数，并对 `0x50` 调用专用析构函数。
这说明这些字段不是普通整数，而是有生命周期的引用/资源成员。

`SoundResource` 的构造函数 `sub_7FF72AE1A110` 会写入大量默认值，例如：

```text
0x28 = 1.0       DefaultVolume
0x34 = 1.0       DefaultFrequencyFactor
0x60 = 0.8       WetLevel
0x64 = 1.0       MinDist
0x68 = 50.0      PressureLevel
0x6C = 1.0       AttenuationLinearity
0x70 = 1.0       AttenuationSlope
0x98 = 360.0     MaxAzimuthDelta
0xC8 = 1.0       SourcePositionExpansionFactor
```

这些默认值与字段表偏移一致，因此可用于校验字段表解析。

## 6. 判断依据的强弱

可以作为确定结论的依据：

- 注册函数同一次调用中绑定了类型描述地址和类型名。
- 类型描述里的大小字段与构造函数写入范围一致。
- 字段表项直接给出字段名字符串、偏移编码和类型描述指针。
- 析构函数对同一偏移执行引用释放或成员析构。
- 业务函数以同一偏移读取字段并参与明确逻辑。

只能作为暂定线索的内容：

- 字段类型描述名本身不足以决定最终 C++ 模板实参和完整布局。
- 带高位标志的 `offset_code` 不能只看低位偏移就解释高位含义。
- 嵌套分组字段，例如 `Sound`、`Wwise`、`Shape`，需要继续追字段解析函数
  或相关访问代码，才能确定完整嵌套结构边界。

## 7. 当前可复用流程

1. 搜索目标类型名字符串。
2. 查找该字符串的数据引用，定位注册函数或静态类型表。
3. 反编译注册函数，找到 `sub_7FF728836920` 调用。
4. 从调用参数确认类型描述地址和类型名绑定。
5. 读取类型描述，记录大小、构造、析构、字段表和注册函数。
6. 读取字段表，每 `0x38` 字节解析一个字段项。
7. 对每个字段项读取字段名字符串和类型描述名。
8. 用构造函数默认值、析构函数释放路径和业务访问代码校验结构布局。
