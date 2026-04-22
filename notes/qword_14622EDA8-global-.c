typedef void *SRWLOCK;

struct GraphSoundInstance;
struct GraphSoundResource;
struct GraphSoundInterface;
struct GraphSoundControlBlock;
struct GraphSoundPlayingSlot;

struct GraphSoundResourceVftable {
    void *slot000;
    void *slot008;
    void *slot010;
    void *slot018;
    GraphSoundInstance *(__fastcall *createInstance)(GraphSoundResource *self);
};

struct GraphSoundBinding {
    void *vftable;
    uint64_t unk08;
    uint64_t unk10;
    uint64_t unk18;
    GraphSoundResource *resource;
    uint32_t unk28;
    int32_t bindCount;
    void *unk30;
};

struct GraphSoundBindingRef {
    GraphSoundBinding *binding;
    uint64_t taggedState;
};

struct GraphSoundResource {
    GraphSoundResourceVftable *vftable;
    volatile int32_t refCount;
    int32_t resourceKind;
    uint64_t keyLo;
    uint64_t keyHi;
    uint8_t unk020[0x0B0];
    int64_t unk0D0[55]; // 覆盖到 +0x287；sub_7FF72AE17BE0 会读取其中 +0x220 起的 int32 索引表，-1 表示无槽
    uint64_t unk288;
    uint64_t unk290;
    uint64_t unk298;
    uint32_t unk2A0;
    uint8_t unk2A4;
    uint8_t pad2A5[0x03];
};

struct GraphSoundControlBlock {
    float accum00; // sub_7FF72AE330E0 每轮按时间步累加到 +0x00/+0x04
    float accum04;
    uint8_t stateByte08;    // sub_7FF72AE330E0 在切换路径中写入 resource+0x290 低字节
    uint8_t reloadCounter09; // sub_7FF72AE330E0 每轮递减；归零前一轮从 resource+0x2A0 低字节重装
    uint8_t stateByte0A;
    uint8_t stateByte0B;
    uint8_t stateByte0C;    // sub_7FF72AE330E0 置位时会清 +0x08/+0x04 并重走 sub_7FF72AE32F40
    uint8_t stateByte0D;
    uint8_t stateByte0E;
    uint8_t unk0F;
    uint8_t stateByte10;    // sub_7FF72AE26830 成功插入 graph 表后写 0
    uint8_t stateByte11;
    uint8_t byte12;
    uint8_t byte13;
    uint8_t stateByte14;
    uint8_t unk15[0x03];
    uint8_t stateByte18; // sub_7FF72AE267F0 / sub_7FF72AE462D0 / sub_7FF72AE269F0 / sub_7FF72AE26830 读取写入
    uint8_t unk19[0x03];
    float scalar1C; // sub_7FF72AE26A90 会同步到实例 +0x330
    float scalar20; // sub_7FF72AE26A90 会同步到实例 +0x334
    float scalar24; // sub_7FF72AE26A90 会同步到实例 +0x338
    void *ptr28; // sub_7FF72AE26B80 / sub_7FF72AE330E0 都会沿这条链访问外部表
    GraphSoundInstance *instance30; // sub_7FF72AE330E0 / sub_7FF72AE33620 从 +48(dec)=+0x30 取出实例
    int32_t listCount38; // sub_7FF72AE330E0 以此为上界遍历 list40
    uint8_t unk3C[0x04];
    void **list40; // sub_7FF72AE330E0 遍历的对象指针数组
    uint8_t unk48[0x08];
    uint32_t copy50; // sub_7FF72AE330E0 从 instance+0x1A0 复制
    float copy54;    // sub_7FF72AE330E0 从 instance+0x1A4 复制
    float copy58;    // sub_7FF72AE330E0 从 instance+0x1A4 复制
    float copy5C;    // sub_7FF72AE330E0 从 instance+0x1A0 复制
    uint32_t copy60; // sub_7FF72AE330E0 从 instance+0x1B0 复制
    uint32_t copy64; // sub_7FF72AE330E0 从 instance+0x1B4 复制
    uint32_t copy68; // sub_7FF72AE330E0 从 instance+0x1B8 复制
    uint32_t copy6C; // sub_7FF72AE330E0 从 instance+0x1AC 复制
    uint8_t unk70[0x04];
    uint8_t copy74; // sub_7FF72AE330E0 从 instance+0x132 复制
    uint8_t unk75[0x4B];
    uint8_t snapshotC0[0x20]; // sub_7FF72AE191E0 写入的控制块内快照
    int32_t tableCountE0; // sub_7FF72AE330E0 以此为上界遍历 tableIndicesE8
    uint8_t unkE4[0x04];
    int32_t *tableIndicesE8; // sub_7FF72AE330E0 逐项读取；值为 -1 时跳过
    uint8_t unkF0[0x10];
    uint32_t stateDword100;
    uint8_t unk104[0x0C];
};

struct GraphSoundPlayingSlot {
    uint32_t playingId;   // sub_7FF72AE45EF0 成功 PostEvent 后写入；a4 路径用于 ExecuteActionOnPlayingID
    uint32_t eventId;     // sub_7FF72AE45EF0 成功 PostEvent 后写入 a1
    uint16_t slotFlags;   // sub_7FF72AE45EF0 会置/清 0x0010/0x0020/0x0080/0x0100/0x0200
    uint16_t useCount;    // sub_7FF72AE45EF0 成功 PostEvent 后递增
    uint8_t lookupKey0C;  // sub_7FF72AE45EF0 用 a5 匹配现有槽
    uint8_t callbackCookie0D; // sub_7FF72AE45EF0 作为 PostEvent 回调 cookie 传出
    uint8_t unk0E[0x02];
};

struct GraphSoundInstanceVftable {
    void *slot000[18];
    char (__fastcall *start)(GraphSoundInstance *self, double a2);
    void *slot098[12];
    int64_t (__fastcall *registerGameObjAndSetPosition)(GraphSoundInstance *self);
    void *slot100[2];
    void (__fastcall *vt272)(GraphSoundInstance *self, int a2);
    void (__fastcall *vt280)(GraphSoundInstance *self, char a2, int a3);
};

struct GraphSoundInstance {
    GraphSoundInstanceVftable *vftable;
    uint8_t unk008[0x018];
    double posX; // sub_7FF72AE58050 以 instance+0x20 作为 3D 位置块入口传入
    double posY;
    double posZ;
    float basisX38; // sub_7FF72AE58A50 与 +0x44..+0x58 一起作为姿态基向量参与计算
    float basisY3C;
    float basisZ40;
    float forwardX;
    float forwardY;
    float forwardZ;
    float upX;
    float upY;
    float upZ;
    uint32_t stateFlags5C;
    uint16_t flags060; // sub_7FF72AE2DAD0 中按位使用：0x0001/0x0002/0x0004/0x0008/0x0040/0x0080/0x0600；sub_7FF72AE18570 使用 bit0 && !bit1；sub_7FF72AE24A50 会重选 bits11..13
    uint16_t orderKey062; // sub_7FF72AE31EA0 运行时以 [instance+0x62] 作为排序主键
    uint32_t resourceDword064;
    float scalar068;
    float scalar06C;
    uint8_t unk070[0x008];
    float scalar078; // sub_7FF72AE24A50 写入：由 +0xB0 位置快照到 qword_7FF732931290 选定点的距离
    uint8_t unk07C[0x0C];
    void *soundStateIIRVftable088;
    uint8_t soundStateIIR088[0x010];
    void *soundStateIIRVftable0A0;
    uint8_t unk0A8[0x008];
    uint8_t transformSnapshot0B0[0x018]; // sub_7FF72AE231A0 先把 +0x20..+0x30 拷到这里，再按 flags060 重算
    uint8_t transformSnapshot0C8[0x018];
    uint8_t unk0E0[0x008];
    float scalar0E8;
    uint8_t unk0EC[0x01C];
    uint8_t resourceByte108;
    uint8_t unk109[0x003];
    float scalar10C; // sub_7FF72AE58A50 在 scalar0E8<=0 分支里作为下限参与 vmax
    float scalar110;
    uint32_t resourceDword114;
    float scalar118;
    float scalar11C; // sub_7FF72AE195E0 在非 bit0/bit1 路径下直接作为倍率基值读取
    uint32_t unk120;
    float scalar124;
    float scalar128;
    float scalar12C; // sub_7FF72AE35A80 在 a5 路径下由传入 float 覆盖，否则取 binding->resource+0x98
    int16_t scalar130; // sub_7FF72AE35A80 把 a7 钳到 [-32768, 32767] 后写入
    uint8_t resourceByte132; // sub_7FF72AE35A80 从 a2+0x20 归一化得到；特定分支会改从 binding->resource->*(+0x20)+0x20 取值
    uint8_t unk133;
    uint32_t derivedState134;
    uint8_t unk138[0x008];
    uint8_t cachedState140[0x020]; // sub_7FF72AE17BE0 用当前 +0x20..+0x30 覆盖这里，作为上一位置快照
    uint8_t initState160[0x010];   // sub_7FF72AE17BE0 在 bit0 && !bit1 路径下写入按 1/dt 缩放的位移向量
    void *unk170;
    GraphSoundBindingRef *binding178;
    uint8_t stateByte180; // sub_7FF72AE1F780 运行时读取 bit1，并在虚调 slot+0x120 后复查；sub_7FF72AE191E0 检查 bit4；sub_7FF72AE35A80 直接重写 bit4/bit5
    uint8_t unk181;
    uint16_t postedSlotCount182; // sub_7FF72AE45EF0 成功 PostEvent 后递增；sub_7FF72AE454D0 清零
    uint16_t stateWord184; // sub_7FF72AE45C60 在无活动 playing slot 时等待归零
    uint16_t lockedStateFlags186; // sub_7FF72AE26450 会置 0x0006；sub_7FF72AE45EF0 成功 PostEvent 后改成 (&0xF66F)|0x0890，随后置 0x0008，消费 a11 时清 0x1000；sub_7FF72AE45C60 会置 0x0010 并清 0x0008、按条件清 0x0800；bit7 由 sub_7FF72AE462D0 在 lock248 保护下读取
    uint8_t unk188[0x004];
    float scalar18C; // sub_7FF72AE45C60 与 scalar190 比较，并作为 SetScalingFactor 的输入之一
    float scalar190; // sub_7FF72AE45C60 与 scalar18C 比较
    uint8_t unk194[0x004];
    float scalar198;
    uint8_t unk19C[0x008];
    float scalar1A4; // sub_7FF72AE195E0 写入 min(1.0f, computedScale)
    uint8_t unk1A8[0x014];
    float scalar1BC; // sub_7FF72AE191E0 在 bit4 路径下参与曲线计算
    uint8_t unk1C0[0x004];
    float scalar1C4;
    float scalar1C8;
    int16_t slotIndex1CC; // sub_7FF72AE2EF40 运行时以 movsx 读取，并以 -1 作为无效值
    uint16_t countdown1CE;
    uint64_t stateTimestamp1D0; // sub_7FF72AE1F780 中当 stateByte180.bit1 变化时写 __rdtsc()
    uint64_t sortKey1D8; // sub_7FF72AE31EA0 中当 orderKey062 相等时作为次排序键
    uint8_t unk1E0;
    uint8_t unk1E1[0x03A];
    uint8_t stateByte21B; // sub_7FF72AE35ED0 把其形参 a1 写到 qword_7FF72E9E9230->+48 所指对象的 +0x21B
    uint8_t notifyByte21C; // sub_7FF72AE45EF0 检测 qword_7FF72E9E9230->+48 所指对象的该字节；非零时通过 a12 返回 1，并清零
    uint8_t unk21D[0x028];
    uint8_t unk245;
    uint8_t pad246[0x02];
    SRWLOCK lock248;
    int32_t playingSlotTableCount; // sub_7FF72AE45EF0 在 lock248 保护下遍历/扩容；sub_7FF7289F7600 作为 push_back 计数并在末尾递增
    int32_t playingSlotTableCapacity; // sub_7FF728A3D270 作为容量上限检查并在扩容成功后更新
    GraphSoundPlayingSlot *playingSlots258; // sub_7FF72AE45EF0 以 16 字节槽数组访问
    uint16_t activePlayingSlotCount; // sub_7FF72AE45EF0 新建槽时递增
    uint16_t unk262;
    uint8_t unk264[0x004];
    uint16_t stateWord268; // sub_7FF72AE45C60 在关键路径里写 1，并等待其归零后再继续
    uint8_t unk26A[0x002];
    float scalar26C; // sub_7FF72AE45C60 与 scalar270/scalar278 共同参与输出总线音量路径
    float scalar270; // sub_7FF72AE45C60 与 scalar26C 比较
    uint8_t unk274[0x002];
    uint8_t stateByte276; // sub_7FF72AE45C60 为零时跳过一段 SetGameObjectOutputBusVolume 路径
    uint8_t unk277;
    float scalar278; // sub_7FF72AE45C60 与 scalar26C 相乘
    uint8_t unk27C[0x02C];
    void *spatialNode2A8;
    uint32_t spatialDword2B0;
    uint32_t spatialDword2B4;
    uint8_t spatialByte2B8;
    uint8_t pendingSpatialRefresh2B9; // ActivateAndRegister 写 1；sub_7FF72AE17BE0 先清 0，位移显著时再置 1；sub_7FF72AE18570 处理后清 0
    uint8_t counter2BA;
    uint8_t unk2BB[0x005];
    void *currentRoom2C0;
    uint8_t unk2C8[0x008];
    uint8_t stateByte2D0; // sub_7FF72AE35EB0 直接写入 a1
    uint8_t stateByte2D1; // sub_7FF72AE35EB0 写 1，表示 +0x2D0 已更新
    uint8_t unk2D2[0x00E];
    uint8_t unk2E0[0x018];
    void *selfRef2F8;
    uint8_t unk300[0x020];
    GraphSoundControlBlock *controlBlock320;
    GraphSoundInterface *soundInterface328;
    float ctorFloat330; // 构造默认 -1.0f；sub_7FF72AE26A90 会用 controlBlock320->scalar1C 覆盖，并通过虚调 slot+88 以 id=21 传出
    float ctorFloat334; // 构造默认 -1.0f；sub_7FF72AE26A90 会用 controlBlock320->scalar20 覆盖，并通过虚调 slot+88 以 id=22 传出
    float ctorFloat338; // 构造默认  1.0f；sub_7FF72AE26A90 会用 controlBlock320->scalar24 覆盖
    uint8_t pad33C[0x04];
};

struct MusicTrackObject {
    uint8_t unk00[0x20];
    uint32_t trackId;
    uint8_t unk24[0x1C];
    GraphSoundResource *primaryDesc;
    void *secondaryDesc;
};

struct MusicEntry {
    uint8_t unk00[0x10];
    MusicTrackObject *track;
    uint8_t unk18[0x1D];
    uint8_t disabled;
    uint8_t unk36[0x02];
};

struct MusicLookupSlot {
    uint32_t trackId;
    uint32_t unk04;
    uint64_t unk08;
    void *slotPtr;
};

struct MusicRuntime {
    uint8_t unk0000[0x1910];

    uint8_t playState;
    uint8_t playMode;
    uint16_t blockFlags;
    uint8_t pad1914[0x04];

    GraphSoundInstance *currentPlayerObj;

    uint32_t sourcePlayingId;
    uint32_t currentTrackId;
    uint32_t lastTrackId;
    uint32_t cursorIndex;
    uint32_t nextPlayOrderIndex;
    uint8_t pad1934[0x04];

    int32_t entryCount;
    MusicEntry *entries;
    uint8_t pad1948[0x08];

    uint32_t *playOrderTrackIds;
    int32_t playOrderCount;
    uint32_t *playOrderRemap;
    int32_t orderCursor;
    uint8_t pad196C[0x04];

    MusicLookupSlot lookup[100];

    uint8_t unk22D0[0x554];

    uint8_t unk2824;
    uint8_t unk2825;
    uint8_t secondaryState;
    uint8_t pad2827;
    uint32_t secondaryStateData;
    void *secondaryPlayerObj;
    uint8_t transitionFlag;
    uint8_t pad2839[0x77];

    uint32_t lastCheckpointSecond;
};

extern MusicRuntime *g_MusicRuntime;
