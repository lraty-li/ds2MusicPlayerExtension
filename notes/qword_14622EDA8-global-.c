typedef void *SRWLOCK;

struct GraphSoundInstance;
struct GraphSoundResource;
struct GraphSoundInterface;

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
    uint16_t flags060;
    uint8_t unk062[0x0DE];
    uint8_t cachedState140[0x020];
    uint8_t initState160[0x010];
    void *unk170;
    GraphSoundBindingRef *binding178;
    uint8_t stateByte180;
    uint8_t unk181[0x05];
    uint16_t stateFlags186;
    uint8_t unk188[0x058];
    uint8_t unk1E0;
    uint8_t unk1E1[0x064];
    uint8_t unk245;
    uint8_t pad246[0x02];
    SRWLOCK lock248;
    uint8_t unk250[0x06A];
    uint8_t counter2BA;
    uint8_t unk2BB[0x065];
    void *controlBlock320;
    GraphSoundInterface *soundInterface328;
    float unk330;
    float unk334;
    float unk338;
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
