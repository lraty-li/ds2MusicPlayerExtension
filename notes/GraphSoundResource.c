/*
 * GraphSoundResource 结构还原记录
 *
 * 依据来源：
 * - 类型描述：word_7FF72E7DDAE0
 * - 类型名：GraphSoundResource
 * - vftable：??_7GraphSoundResource@@6B@ at 0x7FF72BBD33B0
 * - 构造包装：sub_7FF72AE3BB80
 * - 构造主体：sub_7FF72AE3BC00
 * - 析构包装：sub_7FF72AE3BB90
 * - 析构主体：sub_7FF72AE3BBA0
 * - 字段表：0x7FF72C9175F0
 *
 * 关系：
 * - GraphSoundResource 继承 SoundResource。
 * - 构造函数先调用 sub_7FF72AE1A110，也就是 SoundResource 初始化函数。
 * - SoundResource 大小为 0xD0，GraphSoundResource 类型描述大小为 0x2B0。
 *
 * 注意：
 * - 0xD0 之前是 SoundResource 基类区域，本文件不重复展开。
 * - 0xD0..0x287 之间由构造函数批量写为 -1，字段表未给出逐项属性名。
 *   这些值可能是运行时变量/参数索引缓存，需结合业务函数继续确认。
 * - GraphProgram 字段的 offset_code 是 0x1000288，低位偏移为 0x288，
 *   高位标志含义未展开。
 */

#include <stdint.h>

typedef struct GraphSoundEvent {
    void *Name;       /* 0x00: String */
    float Time;       /* 0x08: float */
    uint8_t pad_0C[4];
} GraphSoundEvent;   /* sizeof = 0x10 */

typedef struct GraphSoundResource {
    uint8_t SoundResource_base[0xD0]; /* 0x000..0x0CF */

    /*
     * 构造函数 sub_7FF72AE3BC00 将 0x0D0..0x280 每 8 字节置为 -1。
     * sub_7FF72AE3D100 会把若干图参数名解析成索引后写入这些槽位，
     * 例如 Position、TerrainPosition、ListenerPosition、Environment、
     * EnvironmentFactor、NeedsToSpawn、AccumulatedMovement、
     * SamplePointIndex、DistanceToListener、DistanceDelta、
     * TimeSinceLastSpawn。
     */
    int64_t graph_param_index_cache[55]; /* 0x0D0..0x287 */

    void *GraphProgram;              /* 0x288: Ref_GraphProgramResource */
    void *Events;                    /* 0x290: Array_GraphSoundEvent */
    uint8_t pad_298[8];              /* 0x298 */
    uint32_t UpdateRate;             /* 0x2A0: EGraphSoundUpdateRate, ctor default 1 */
    uint8_t SaveVoiceStateOnSuspend; /* 0x2A4: bool */
    uint8_t pad_2A5[11];             /* 0x2A5 */
} GraphSoundResource;                /* sizeof = 0x2B0 */

typedef char static_assert_GraphSoundEvent_size[
    sizeof(GraphSoundEvent) == 0x10 ? 1 : -1
];

typedef char static_assert_GraphSoundResource_size[
    sizeof(GraphSoundResource) == 0x2B0 ? 1 : -1
];

/*
 * GraphSoundResource 字段表 0x7FF72C9175F0 解析结果：
 *
 * 偏移      字段名                    类型线索
 * 0x288     GraphProgram              Ref_GraphProgramResource
 * 0x290     Events                    Array_GraphSoundEvent
 * 0x2A0     UpdateRate                EGraphSoundUpdateRate
 * 0x2A4     SaveVoiceStateOnSuspend   bool
 *
 * GraphProgram 字段细节：
 * - 字段表项位于 0x7FF72C917628。
 * - 字段名字符串为 GraphProgram。
 * - offset_code 为 0x1000288，低位偏移为 0x288。
 * - 字段类型描述为 0x7FF72C88A930，名称为 Ref_GraphProgramResource。
 * - 该 Ref 包装指向 GraphProgramResource 类型描述 0x7FF72E4FD260。
 * - 同一静态区域还存在 StreamingRef_GraphProgramResource 描述，
 *   但 GraphProgram 字段表项没有指向 StreamingRef 那一项。
 * - 构造函数将 0x288 清零；析构函数检查 a1[81]，非空时释放引用。
 *
 * GraphSoundEvent 字段表 0x7FF72C917480 解析结果：
 *
 * 偏移      字段名    类型线索
 * 0x00      Name      String
 * 0x08      Time      float
 */
