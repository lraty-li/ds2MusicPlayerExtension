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
    int64_t unk0D0[55];
    uint64_t unk288;
    uint64_t unk290;
    uint64_t unk298;
    uint32_t unk2A0;
    uint8_t unk2A4;
    uint8_t pad2A5[0x03];
};

struct GraphSoundControlBlock {
    uint64_t unk00;
    uint64_t unk08;
    uint8_t unk10;
    uint8_t byte11;
    uint8_t byte12;
    uint8_t byte13;
    uint8_t unk14[0x04];
    uint8_t stateByte18;
    uint8_t unk19[0xE7];
    uint32_t stateDword100;
    uint8_t unk104[0x0C];
};

struct GraphSoundPlayingSlot {
    uint32_t playingId;
    uint32_t eventId;
    uint16_t slotFlags;
    uint16_t useCount;
    uint8_t lookupKey0C;
    uint8_t callbackCookie0D;
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
    double posX;
    double posY;
    double posZ;
    uint8_t unk038[0x00C];
    float forwardX;
    float forwardY;
    float forwardZ;
    float upX;
    float upY;
    float upZ;
    uint32_t stateFlags5C;
    uint16_t flags060; // sub_7FF72AE2DAD0 中按位使用：0x0001/0x0002/0x0004/0x0008/0x0040/0x0080/0x0600；sub_7FF72AE18570 使用 bit0 && !bit1
    uint16_t orderKey062;
    uint32_t resourceDword064;
    uint8_t unk068[0x010];
    float scalar078;
    uint8_t unk07C[0x0C];
    void *soundStateIIRVftable088;
    uint8_t soundStateIIR088[0x010];
    void *soundStateIIRVftable0A0;
    uint8_t unk0A8[0x008];
    uint8_t transformSnapshot0B0[0x018];
    uint8_t transformSnapshot0C8[0x018];
    uint8_t unk0E0[0x028];
    uint8_t resourceByte108;
    uint8_t unk109[0x003];
    uint32_t resourceDword10C;
    float scalar110;
    uint32_t resourceDword114;
    float scalar118;
    uint32_t resourceDword11C;
    uint8_t unk120[0x008];
    float scalar128;
    uint32_t resourceDword12C;
    uint8_t resourceByte130;
    uint8_t resourceByte131;
    uint8_t resourceByte132;
    uint8_t unk133;
    uint32_t derivedState134;
    uint8_t unk138[0x008];
    uint8_t cachedState140[0x020];
    uint8_t initState160[0x010];
    void *unk170;
    GraphSoundBindingRef *binding178;
    uint8_t stateByte180;
    uint8_t unk181;
    uint16_t postedSlotCount182;
    uint16_t stateWord184;
    uint16_t lockedStateFlags186; // sub_7FF72AE26450 会置 0x0006；bit7 由 sub_7FF72AE462D0 在 lock248 保护下读取
    uint8_t unk188[0x040];
    float scalar1C8;
    uint16_t slotIndex1CC;
    uint16_t countdown1CE;
    uint8_t unk1D0[0x008];
    uint64_t sortKey1D8;
    uint8_t unk1E0;
    uint8_t unk1E1[0x064];
    uint8_t unk245;
    uint8_t pad246[0x02];
    SRWLOCK lock248;
    int32_t playingSlotTableCount;
    int32_t unk254;
    GraphSoundPlayingSlot *playingSlots258;
    uint16_t activePlayingSlotCount;
    uint16_t unk262;
    uint8_t unk264[0x044];
    void *spatialNode2A8;
    uint32_t spatialDword2B0;
    uint32_t spatialDword2B4;
    uint8_t spatialByte2B8;
    uint8_t pendingSpatialRefresh2B9; // ActivateAndRegister 写 1；sub_7FF72AE18570 处理后清 0
    uint8_t counter2BA;
    uint8_t unk2BB[0x005];
    void *currentRoom2C0;
    uint8_t unk2C8[0x008];
    uint8_t stateByte2D0;
    uint8_t stateByte2D1;
    uint8_t unk2D2[0x00E];
    uint8_t unk2E0[0x018];
    void *selfRef2F8;
    uint8_t unk300[0x020];
    GraphSoundControlBlock *controlBlock320;
    GraphSoundInterface *soundInterface328;
    float ctorFloat330;
    float ctorFloat334;
    float ctorFloat338;
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
