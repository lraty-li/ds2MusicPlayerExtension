/*
 * GraphProgramResource 结构还原记录
 *
 * 依据来源：
 * - 注册函数：sub_7FF72AB7A0A0
 * - 类型描述：unk_7FF72E4FD260
 * - 类型名：GraphProgramResource
 * - 构造包装：sub_7FF72AB7A070
 * - 构造主体：sub_7FF72A946990
 * - 析构包装：sub_7FF72AB7A080
 * - 析构主体：sub_7FF72A92B5F0
 * - 字段表：0x7FF72C88AE30
 *
 * GraphSoundResource.GraphProgram 字段：
 * - 位于 GraphSoundResource + 0x288。
 * - 字段类型描述为 0x7FF72C88A930。
 * - 字段类型名为 Ref_GraphProgramResource。
 * - Ref 指向本文件记录的 GraphProgramResource 类型描述。
 */

#include <stdint.h>

typedef struct GraphProgramResource {
    void *vftable;                    /* 0x00 */
    uint8_t ProgramResource_base[0x48]; /* 0x08..0x4F，基类/公共资源区待展开 */

    void *StateParameters;            /* 0x50: ProgramParameterList */
    uint8_t unknown_58[0x60];         /* 0x58..0xB7 */
    void *ExposedDataResource;        /* 0xB8: Ref_NodeConstantsResource */
    void *EventFunctionIndexMap;      /* 0xC0: Array_EventFunctionMapping */
    uint8_t unknown_C8[0x18];         /* 0xC8..0xDF */
    void *RequiredVirtualTypes;       /* 0xE0: Array_Ref_VirtualRTTIResource */
    uint8_t unknown_E8[8];            /* 0xE8 */
} GraphProgramResource;               /* sizeof = 0xF0 */

typedef char static_assert_GraphProgramResource_size[
    sizeof(GraphProgramResource) == 0xF0 ? 1 : -1
];

/*
 * 字段表 0x7FF72C88AE30 解析结果：
 *
 * 分组名：Graph
 *
 * 偏移      字段名                  类型线索
 * 0x50      StateParameters         ProgramParameterList
 * 0xB8      ExposedDataResource     Ref_NodeConstantsResource
 * 0xC0      EventFunctionIndexMap   Array_EventFunctionMapping
 * 0xE0      RequiredVirtualTypes    Array_Ref_VirtualRTTIResource
 *
 * 构造函数 sub_7FF72A946990：
 * - 设置 GraphProgramResource vftable。
 * - 将 0x08、0x10、0x18、0x20、0x28、0x30、0x38 清零。
 * - 0x40 写入 1，0x42 写入 0。
 * - 0x48 写入第二个 GraphProgramResource vftable 指针。
 * - 0x50 起到 0xE8 的成员区域初始化为 0。
 *
 * 析构函数 sub_7FF72A92B5F0：
 * - 对 0xE0、0xD0、0xC0、0xA8、0x90 等数组/容器成员调用析构。
 * - 对 0xB8 和 0x30 的引用成员执行引用释放。
 * - 最后切回 ProgramResource vftable，再切回 RTTIObject vftable。
 *
 * 注意：
 * - 0x58..0xB7 与 0xC8..0xDF 仍有多个基类/容器成员，字段表当前只给出
 *   上面四个明确属性名。
 * - EventFunctionIndexMap 的元素类型 EventFunctionMapping 可继续展开；
 *   当前只确认字段本身是数组类型。
 */
