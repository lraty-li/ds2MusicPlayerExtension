/*
 * NodeConstantsResource 结构还原记录
 *
 * 依据来源：
 * - 类型描述：0x7FF72E4FE2F0
 * - 类型名：NodeConstantsResource
 * - vftable：??_7NodeConstantsResource@@6B@ at 0x7FF72BB93488
 * - 构造函数：sub_7FF72AB82860
 * - 析构包装：sub_7FF72AB828D0
 * - 析构主体：sub_7FF72AB813C0
 * - 字段表：0x7FF72C88C970
 *
 * GraphProgramResource.ExposedDataResource：
 * - 位于 GraphProgramResource + 0xB8。
 * - 字段类型为 Ref_NodeConstantsResource。
 * - Ref 包装指向本文件记录的 NodeConstantsResource 类型描述。
 */

#include <stdint.h>

typedef struct NodeConstantsResource {
    void *vftable;                    /* 0x00 */
    uint8_t rtti_or_base_08[0x18];    /* 0x08..0x1F */
    uint8_t Parameters[0x68];         /* 0x20: ProgramParameterList */
    void *ExposedObjectsIndices;      /* 0x88: Array_int */
    uint8_t container_90[0x08];       /* 0x90..0x97 */
    void *ExposedUUIDRefIndices;      /* 0x98: Array_int */
    uint8_t container_A0[0x08];       /* 0xA0..0xA7 */
} NodeConstantsResource;              /* sizeof = 0xA8 */

typedef char static_assert_NodeConstantsResource_size[
    sizeof(NodeConstantsResource) == 0xA8 ? 1 : -1
];

/*
 * 字段表 0x7FF72C88C970 解析结果：
 *
 * 偏移      字段名                    类型线索
 * 0x20      Parameters                ProgramParameterList
 * 0x88      ExposedObjectsIndices     Array_int
 * 0x98      ExposedUUIDRefIndices     Array_int
 *
 * ProgramParameterList 类型描述 0x7FF72E7E6A40：
 * - 类型大小为 0x68。
 * - 在 NodeConstantsResource 中内嵌于 0x20..0x87。
 * - 字段表位于 0x7FF72C927250。
 *
 * ProgramParameterList 内部字段：
 *
 * 外层绝对偏移  内层偏移  字段名                     类型线索
 * 0x20          0x00      Parameters                 cptr_SpawnSoundNodeStateResource
 * 0x30          0x10      DefaultBinaryValues        未完全展开
 * 0x40          0x20      DefaultSoftLinkedObjects   未完全展开
 * 0x50          0x30      DefaultHardLinkedObjects   未完全展开
 * 0x60          0x40      DefaultUUIDRefs            未完全展开
 *
 * 因此运行时看到的：
 * - Parameters[0] = WwiseID
 * - Parameters[1] = SoundGroup
 * 是 NodeConstantsResource + 0x20 的 ProgramParameterList 内部
 * Parameters 数组内容。
 *
 * DefaultSoftLinkedObjects[0] 和 [1] 则在同一个 ProgramParameterList
 * 的 +0x20 子字段中，对应外层 NodeConstantsResource + 0x40。
 *
 * 构造函数 sub_7FF72AB82860：
 * - 写入 NodeConstantsResource vftable。
 * - 清零 0x08、0x10、0x18。
 * - 清零 0x20 起的成员区域，直到 0xA0。
 *
 * 析构函数 sub_7FF72AB813C0：
 * - 对 0x98、0x88、0x78、0x60、0x50、0x40、0x30、0x20 等
 *   容器/列表成员调用析构函数。
 * - 最后切回 RTTIObject vftable。
 *
 * 注意：
 * - 字段表只暴露了上面 3 个属性名；0x28..0x87 和 0xA0..0xA7
 *   仍按内部容器/保留区记录。
 * - ExposedObjectsIndices 与 ExposedUUIDRefIndices 类型描述相同，
 *   静态类型名线索为 Array_int。
 */
