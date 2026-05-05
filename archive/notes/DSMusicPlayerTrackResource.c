/*
 * DSMusicPlayerTrackResource 结构还原记录
 *
 * 依据来源全部来自本次 IDA MCP 只读分析：
 * - 类型注册：sub_7FF7293A7590 使用 "DSMusicPlayerTrackResource"、
 *   "DSMusicPlayerTrackResourceSymbols" 和
 *   "UUIDRef_DSMusicPlayerTrackResource" 注册类型。
 * - 类型描述：unk_7FF72CABA950 的对象大小字段为 0x60，字段表指向
 *   0x7FF72C50D020。
 * - 构造：sub_7FF7293A7530 清零基类区与成员区，并写入
 *   DSMusicPlayerTrackResource vftable。
 * - 析构：sub_7FF72939F620 释放 0x30、0x38、0x40、0x48、0x58 的
 *   引用成员，并对 0x50 调用专用析构函数 sub_7FF72AE68DB0。
 *
 * 相关引用：
 * - sub_7FF72939F610：返回 DSMusicPlayerTrackResource 类型描述指针。
 * - sub_7FF72939F620：析构/释放成员引用。
 * - sub_7FF72939F7D0：deleting destructor 包装。
 * - sub_7FF7293A7530：placement constructor/初始化函数。
 * - sub_7FF7293A7570：析构包装，直接调用 sub_7FF72939F620。
 * - sub_7FF7293A7590：符号与 UUIDRef 注册函数。
 * - ??_7DSMusicPlayerTrackResource@@6B@ at 0x7FF72B992C90：vftable。
 * - unk_7FF72CABA950：类型描述；字段表位于 0x7FF72C50D020。
 *
 * 字符串/类型表中的其它引用形态：
 * - Ref_DSMusicPlayerTrackResource
 * - UUIDRef_DSMusicPlayerTrackResource
 * - Array_UUIDRef_DSMusicPlayerTrackResource
 * - cptr_DSMusicPlayerTrackResource
 * - Array_cptr_DSMusicPlayerTrackResource
 * - Array_Ref_DSMusicPlayerTrackResource
 *
 * 注意：
 * - RTTIObject 基类字段尚未展开；这里只按构造函数写入范围保留。
 * - 0x10028 是字段表中 Flag 的编码值，低位偏移为 0x28，
 *   高位标志含义未继续展开，因此结构中按 uint8_t 表示实际存储。
 * - 引用类型的精确 C++ 模板名未从类型系统改名；这里用占位指针表达
 *   已确认的偏移、生命周期和资源类型名。
 */

#include <stdint.h>

typedef struct RTTIObjectPrefix {
    void *vftable;          /* 0x00 */
    uint64_t base_08;       /* 0x08 */
    uint64_t base_10;       /* 0x10 */
    uint32_t base_18;       /* 0x18 */
    uint8_t base_1C[4];     /* 0x1C */
} RTTIObjectPrefix;

typedef struct DSMusicPlayerTrackResource {
    RTTIObjectPrefix rtti;          /* 0x00 */
    uint32_t TrackId;               /* 0x20: uint32 */
    uint16_t Seconds;               /* 0x24: uint16 */
    int16_t MenuDisplayPriority;    /* 0x26: int16 */
    uint8_t Flag;                   /* 0x28: uint8, 字段表编码为 0x10028 */
    uint8_t pad_29[7];              /* 0x29 */
    void *AlbumResource;            /* 0x30: Ref_DSMusicPlayerAlbumResource */
    void *TitleText;                /* 0x38: Ref_LocalizedTextResource */
    void *SoundResource;            /* 0x40: Ref_SoundResource */
    void *TrialSoundResource;       /* 0x48: Ref_SoundResource */
    void *JacketUITexture;          /* 0x50: StreamingRef_UITexture/Ref_UITexture */
    void *OpenConditionFact;        /* 0x58: Ref_BooleanFact */
} DSMusicPlayerTrackResource;

typedef char static_assert_DSMusicPlayerTrackResource_size[
    sizeof(DSMusicPlayerTrackResource) == 0x60 ? 1 : -1
];

/*
 * 字段表 0x7FF72C50D020 解析结果：
 *
 * 偏移    字段名                  类型线索
 * 0x20    TrackId                 uint32
 * 0x24    Seconds                 uint16
 * 0x26    MenuDisplayPriority     int16
 * 0x28    Flag                    uint8，字段表偏移编码为 0x10028
 * 0x30    AlbumResource           Ref_DSMusicPlayerAlbumResource
 * 0x38    TitleText               Ref_LocalizedTextResource
 * 0x40    SoundResource           Ref_SoundResource
 * 0x48    TrialSoundResource      Ref_SoundResource
 * 0x50    JacketUITexture         StreamingRef_UITexture / Ref_UITexture
 * 0x58    OpenConditionFact       Ref_BooleanFact
 */
