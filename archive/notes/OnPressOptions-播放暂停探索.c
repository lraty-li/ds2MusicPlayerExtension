// OnPressOptions / 播放暂停链补充类型
// 仅保留 qword_14622EDA8-global-.c 里尚未落下的类型

typedef struct GraphSoundInstance GraphSoundInstance;

// 运行已命中

typedef struct MusicRuntimeEntry38 {
    void *unk00;                    // +0x00
    unsigned long long unk08;       // +0x08
    unsigned long long unk10;       // +0x10
    GraphSoundInstance *obj18;      // +0x18
    GraphSoundInstance *obj20;      // +0x20
    unsigned long long unk28;       // +0x28
    unsigned int state30;           // +0x30
    unsigned int unk34;             // +0x34
} MusicRuntimeEntry38; // 0x38

// 静态候选

typedef struct MusicRuntimeEntryCore {
    unsigned char unk00[0x20];
    unsigned int trackId;           // +0x20
    unsigned char unk24[0x1C];
    void *obj40Source;              // +0x40
    void *obj48Source;              // +0x48
} MusicRuntimeEntryCore;

typedef struct MusicRuntimeEntryDesc {
    unsigned char unk00[0x10];
    MusicRuntimeEntryCore *entryCore; // +0x10
    unsigned char unk18[0x1D];
    unsigned char disabled;         // +0x35
    unsigned char unk36[0x02];
} MusicRuntimeEntryDesc;
