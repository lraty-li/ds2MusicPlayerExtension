// local variable allocation has failed, the output may be wrong!
__int64 __fastcall sub_7FF61581F510(__int64 a1, double _XMM1_8)
{
  int v4; // edx
  int v5; // edx
  int v6; // edx
  int v7; // edx
  int v8; // edx
  int v9; // edx
  __int64 v12; // rbx
  __int64 v15; // rbx
  __int64 v18; // rbx
  char v20[8]; // [rsp+60h] [rbp-A0h] BYREF
  __int64 v21; // [rsp+68h] [rbp-98h]
  __int64 v22; // [rsp+70h] [rbp-90h]
  const char *v23; // [rsp+78h] [rbp-88h]
  __int64 v24; // [rsp+80h] [rbp-80h]
  char v25; // [rsp+88h] [rbp-78h]
  bool (__fastcall *v26)(AK::WriteBytesCount *__hidden, int); // [rsp+90h] [rbp-70h]
  const char *v27; // [rsp+98h] [rbp-68h]
  __int64 v29; // [rsp+B0h] [rbp-50h] BYREF
  __int64 v30; // [rsp+B8h] [rbp-48h]
  __int64 v34; // [rsp+F0h] [rbp-10h] BYREF
  __int64 v35; // [rsp+F8h] [rbp-8h]

  sub_7FF61356B410(a1 + 24);
  sub_7FF61356B340(a1 + 24);
  LOBYTE(v4) = 2;
  *(_QWORD *)(a1 + 16) = "Music";
  sub_7FF613566920(
    a1,
    v4,
    (unsigned int)&unk_7FF6179D3690,
    (unsigned int)"MusicProject",
    (__int64)"MusicProject",
    0,
    0,
    0,
    0,
    1,
    0);
  sub_7FF613567210(a1, (char **)&unk_7FF6179D3690, (__int64)"UUIDRef_MusicProject", 1);
  LOBYTE(v5) = 2;
  sub_7FF613566920(
    a1,
    v5,
    (unsigned int)&unk_7FF6179D3510,
    (unsigned int)"MusicCue",
    (__int64)"MusicCue",
    0,
    0,
    0,
    0,
    1,
    0);
  sub_7FF613567210(a1, (char **)&unk_7FF6179D3510, (__int64)"UUIDRef_MusicCue", 1);
  LOBYTE(v6) = 2;
  sub_7FF613566920(
    a1,
    v6,
    (unsigned int)&unk_7FF6179D2890,
    (unsigned int)"MusicCueContainer",
    (__int64)"MusicCueContainer",
    0,
    0,
    0,
    0,
    1,
    0);
  sub_7FF613567210(a1, (char **)&unk_7FF6179D2890, (__int64)"UUIDRef_MusicCueContainer", 1);
  LOBYTE(v7) = 2;
  sub_7FF613566920(
    a1,
    v7,
    (unsigned int)&unk_7FF6179D1A00,
    (unsigned int)"MusicRegionType",
    (__int64)"MusicRegionType",
    0,
    0,
    0,
    0,
    1,
    0);
  sub_7FF613567210(a1, (char **)&unk_7FF6179D1A00, (__int64)"UUIDRef_MusicRegionType", 1);
  LOBYTE(v8) = 1;
  sub_7FF613566920(
    a1,
    v8,
    (unsigned int)&unk_7FF6175AF528,
    (unsigned int)"EMusicKey",
    (__int64)"EMusicKey",
    0,
    0,
    (__int64)sub_7FF613571190,
    0,
    3,
    (__int64)&unk_7FF6170035E0);
  LOBYTE(v9) = 1;
  *(double *)&_XMM0 = sub_7FF613566920(
                        a1,
                        v9,
                        (unsigned int)&unk_7FF6175B1A28,
                        (unsigned int)"EMusicScale",
                        (__int64)"EMusicScale",
                        0,
                        0,
                        (__int64)sub_7FF613571190,
                        0,
                        3,
                        (__int64)&unk_7FF6170035E0);
  __asm { vpxor   xmm0, xmm0, xmm0 }
  v22 = *(_QWORD *)(a1 + 16);
  v23 = "MusicManager::sExportedGetMusicStatus";
  v27 = "GetMusicStatus";
  __asm { vpxor   xmm1, xmm1, xmm1 }
  v26 = (bool (__fastcall *)(AK::WriteBytesCount *__hidden, int))AK::MemoryMgr::StartProfileThreadUsage;
  v24 = 0;
  v25 = 0;
  __asm { vmovdqu [rbp+30h+var_90], xmm0 }
  v29 = 0;
  v30 = 0;
  __asm
  {
    vmovdqu [rbp+30h+var_70], xmm0
    vmovdqu [rbp+30h+var_60], xmm1
    vmovdqu [rbp+30h+var_50], xmm0
  }
  v34 = 0;
  v35 = 0;
  __asm { vmovdqu [rbp+30h+var_30], xmm0 }
  v20[0] = 5;
  v21 = 0;
  sub_7FF6158250A0(&v29);
  v12 = *(_QWORD *)(a1 + 32) + 176LL * (int)sub_7FF61356B010(a1 + 24, v20);
  sub_7FF61356ACF0(&v34);
  *(double *)&_XMM0 = sub_7FF61356ACF0(&v29);
  *(_BYTE *)(v12 + 40) = 0;
  __asm { vpxor   xmm0, xmm0, xmm0 }
  v22 = *(_QWORD *)(a1 + 16);
  v23 = "MusicManager::sExportedIsMusicInRegion";
  v27 = "IsMusicInRegion";
  __asm { vpxor   xmm1, xmm1, xmm1 }
  v26 = (bool (__fastcall *)(AK::WriteBytesCount *__hidden, int))AK::MemoryMgr::StartProfileThreadUsage;
  v24 = 0;
  v25 = 0;
  __asm { vmovdqu [rbp+30h+var_90], xmm0 }
  v29 = 0;
  v30 = 0;
  __asm
  {
    vmovdqu [rbp+30h+var_70], xmm0
    vmovdqu [rbp+30h+var_60], xmm1
    vmovdqu [rbp+30h+var_50], xmm0
  }
  v34 = 0;
  v35 = 0;
  __asm { vmovdqu [rbp+30h+var_30], xmm0 }
  v20[0] = 5;
  v21 = 0;
  sub_7FF615825790(&v29);
  v15 = *(_QWORD *)(a1 + 32) + 176LL * (int)sub_7FF61356B010(a1 + 24, v20);
  sub_7FF61356ACF0(&v34);
  *(double *)&_XMM0 = sub_7FF61356ACF0(&v29);
  *(_BYTE *)(v15 + 40) = 0;
  __asm { vpxor   xmm0, xmm0, xmm0 }
  v22 = *(_QWORD *)(a1 + 16);
  v23 = "MusicManager::sExportedIsCueContainedIn";
  v27 = "IsCueContainedIn";
  __asm { vpxor   xmm1, xmm1, xmm1 }
  v26 = AK::WriteBytesCount::Reserve;
  v24 = 0;
  v25 = 0;
  __asm { vmovdqu [rbp+30h+var_90], xmm0 }
  v29 = 0;
  v30 = 0;
  __asm
  {
    vmovdqu [rbp+30h+var_70], xmm0
    vmovdqu [rbp+30h+var_60], xmm1
    vmovdqu [rbp+30h+var_50], xmm0
  }
  v34 = 0;
  v35 = 0;
  __asm { vmovdqu [rbp+30h+var_30], xmm0 }
  v20[0] = 5;
  v21 = 0;
  sub_7FF61486E090(&v29);
  v18 = *(_QWORD *)(a1 + 32) + 176LL * (int)sub_7FF61356B010(a1 + 24, v20);
  sub_7FF61356ACF0(&v34);
  sub_7FF61356ACF0(&v29);
  *(_BYTE *)(v18 + 40) = 0;
  return sub_7FF61356B260(a1 + 24);
}