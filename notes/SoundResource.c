struct SoundResource {
    void *vftable;                 // 0x00

    // Sound / Wwise 名称相关区域，字段表有 Sound、Group。
    // 其中 Group 在 0x20，类型线索是 cptr_WwiseName。
    uint8_t sound_name_area[0x18]; // 0x08
    void *Group;                   // 0x20: cptr_WwiseName

    float DefaultVolume;           // 0x28: LinearGainFloat
    float DefaultLfeVolume;        // 0x2C: LinearGainFloat
    float DefaultAngle;            // 0x30
    float DefaultFrequencyFactor;  // 0x34

    uint8_t pad_38[0x08];          // 0x38

    // 类型线索：SoundShape
    uint8_t Shape[0x20];           // 0x40

    float WetLevel;                // 0x60: LinearGainFloat
    float MinDist;                 // 0x64
    float PressureLevel;           // 0x68
    float AttenuationLinearity;    // 0x6C
    float AttenuationSlope;        // 0x70

    bool DefaultLooping;           // 0x74
    bool UsesHDRSystem;            // 0x75
    bool UsesRaycast;              // 0x76
    bool AffectedByTimeScale;      // 0x77
    uint8_t InstanceLimitMode;     // 0x78: ESoundInstanceLimitMode
    uint8_t InstanceLimit;         // 0x79
    uint16_t BitField;             // 0x7A
    float InitialRMS;              // 0x7C

    float WetMinRange;             // 0x80
    float WetMaxRange;             // 0x84
    float WetLevelBias;            // 0x88
    float OcclusionFactor;         // 0x8C
    float ObstructionFactor;       // 0x90
    float DopplerFactor;           // 0x94
    float MaxAzimuthDelta;         // 0x98

    uint8_t unknown_9C[0x14];      // 0x9C

    // 析构函数对 0xA0 调用 sub_7FF72889DFF0，说明这里是带生命周期的成员。
    uint8_t managed_A0[0x10];      // 0xA0

    void *ResourceName;            // 0xB0: String / TIPath 线索，字段编码带 0x400
    uint32_t ResourceNameHash;     // 0xB8: uint32，字段编码带 0x400
    uint8_t SoundSpacializedType;  // 0xBC: ESoundSpacializedType
    bool AllowMultiplePlayRequest; // 0xBD
    bool CancelPriorityCalc;       // 0xBE
    uint8_t pad_BF;                // 0xBF

    float MaxDist;                 // 0xC0
    bool CancelSoundZoneOcclusionAndObstruction; // 0xC4
    bool StopOnSkip;               // 0xC5
    uint8_t pad_C6[2];             // 0xC6
    float SourcePositionExpansionFactor; // 0xC8
    uint8_t pad_CC[4];             // 0xCC
}; // sizeof = 0xD0
