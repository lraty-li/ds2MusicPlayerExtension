// OnPressOptions / 播放暂停链相关类型

// 运行已命中

typedef struct GraphSoundControlBlock {
    unsigned char unk000[0x13];
    unsigned char byte13;          // +0x13
    unsigned char unk014[0xEC];
    unsigned int dword100;         // +0x100
} GraphSoundControlBlock;

typedef struct GraphSoundInstance {
    void *vftable;                         // +0x000
    unsigned char unk008[0x178];
    unsigned char stateByte180;           // +0x180
    unsigned char unk181[0x139];
    unsigned char counter2BA;             // +0x2BA
    unsigned char unk2BB[0x64];
    GraphSoundControlBlock *controlBlock320; // +0x320
} GraphSoundInstance;

typedef struct MusicRuntimeEntry38 {
    void *unk00;                          // +0x00
    unsigned long long unk08;            // +0x08
    unsigned long long unk10;            // +0x10
    GraphSoundInstance *obj18;           // +0x18
    GraphSoundInstance *obj20;           // +0x20
    unsigned long long unk28;            // +0x28
    unsigned int state30;                // +0x30
    unsigned int unk34;                  // +0x34
} MusicRuntimeEntry38; // 0x38

typedef struct MusicRuntime {
    unsigned char unk0000[0x1910];
    unsigned char playState;             // +0x1910
    unsigned char playOrderMode;         // +0x1911
    unsigned char unk1912[0x6];
    GraphSoundInstance *currentPlayerObj; // +0x1918
    unsigned char unk1920[0x4];
    unsigned int currentTrackId;         // +0x1924
    unsigned int lastTrackId;            // +0x1928
    unsigned char unk192C[0x4];
    unsigned int currentPlayOrderIndex;  // +0x1930
    unsigned char unk1934[0x14];
    unsigned int entryCount;             // +0x1948
    unsigned char unk194C[0x0F4C];
    MusicRuntimeEntry38 *entries2898;    // +0x2898
    unsigned int entryCount28A4;         // +0x28A4
    unsigned char unk28A8[0x8];
    unsigned int lastPositionSeconds;    // +0x28B0
    unsigned char unk28B4[0x2];
    unsigned char unk28B6;               // +0x28B6
} MusicRuntime;

// 静态候选

typedef void (__fastcall *GraphSoundInstance_Vt110)(
    GraphSoundInstance *self,
    int a2
);

typedef void (__fastcall *GraphSoundInstance_Vt118)(
    GraphSoundInstance *self,
    char a2,
    int a3
);
