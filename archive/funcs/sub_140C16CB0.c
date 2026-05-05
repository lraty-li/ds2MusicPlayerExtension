// local variable allocation has failed, the output may be wrong!
__int64 __fastcall sub_140C16CB0(__int64 a1, double _XMM1_8)
{
  int v4; // edx
  __int64 v7; // rbx
  __int64 v10; // rbx
  __int64 v11; // rdx
  __int64 v12; // r8
  __int64 v13; // rdx
  __int64 v14; // r8
  __int64 v15; // rdx
  __int64 v16; // r8
  char v18[8]; // [rsp+60h] [rbp-A0h] BYREF
  __int64 v19; // [rsp+68h] [rbp-98h]
  __int64 v20; // [rsp+70h] [rbp-90h]
  const char *v21; // [rsp+78h] [rbp-88h]
  __int64 v22; // [rsp+80h] [rbp-80h]
  char v23; // [rsp+88h] [rbp-78h]
  __int64 (__fastcall *v24)(); // [rsp+90h] [rbp-70h]
  const char *v25; // [rsp+98h] [rbp-68h]
  __int64 v27; // [rsp+B0h] [rbp-50h] BYREF
  __int64 v28; // [rsp+B8h] [rbp-48h]
  __int64 v32; // [rsp+F0h] [rbp-10h] BYREF
  __int64 v33; // [rsp+F8h] [rbp-8h]

  sub_1400AB0E0(a1 + 24);
  sub_1400AB010(a1 + 24);
  *(_QWORD *)(a1 + 16) = "DSMusicPlayerNode";
  LOBYTE(v4) = 1;
  *(double *)&_XMM0 = sub_1400A65F0(
                        a1,
                        v4,
                        (unsigned int)&unk_143D79AD0,
                        (unsigned int)"EDSMusicPlayerBanReason",
                        (__int64)"EDSMusicPlayerBanReason",
                        0,
                        0,
                        (__int64)sub_1400B0E60,
                        0,
                        3,
                        (__int64)&unk_143B41C00);
  __asm { vpxor   xmm0, xmm0, xmm0 }
  v20 = *(_QWORD *)(a1 + 16);
  v21 = "music_player_nodes::sSetMenuOpen";
  v25 = "SetMenuOpen";
  __asm { vpxor   xmm1, xmm1, xmm1 }
  v24 = (__int64 (__fastcall *)())AK::MemoryMgr::StartProfileThreadUsage;
  v22 = 0;
  v23 = 0;
  __asm { vmovdqu [rbp+30h+var_90], xmm0 }
  v27 = 0;
  v28 = 0;
  __asm
  {
    vmovdqu [rbp+30h+var_70], xmm0
    vmovdqu [rbp+30h+var_60], xmm1
    vmovdqu [rbp+30h+var_50], xmm0
  }
  v32 = 0;
  v33 = 0;
  __asm { vmovdqu [rbp+30h+var_30], xmm0 }
  v18[0] = 5;
  v19 = 0;
  sub_14049F190(&v27);
  v7 = *(_QWORD *)(a1 + 32) + 176LL * (int)sub_1400AACE0(a1 + 24, v18);
  sub_1400AA9C0(&v32);
  *(double *)&_XMM0 = sub_1400AA9C0(&v27);
  *(_BYTE *)(v7 + 40) = 0;
  __asm { vpxor   xmm0, xmm0, xmm0 }
  v20 = *(_QWORD *)(a1 + 16);
  v21 = "music_player_nodes::sIsMenuOpen";
  v25 = "IsMenuOpen";
  __asm { vpxor   xmm1, xmm1, xmm1 }
  v24 = sub_140C167B0;
  v22 = 0;
  v23 = 0;
  __asm { vmovdqu [rbp+30h+var_90], xmm0 }
  v27 = 0;
  v28 = 0;
  __asm
  {
    vmovdqu [rbp+30h+var_70], xmm0
    vmovdqu [rbp+30h+var_60], xmm1
    vmovdqu [rbp+30h+var_50], xmm0
  }
  v32 = 0;
  v33 = 0;
  __asm { vmovdqu [rbp+30h+var_30], xmm0 }
  v18[0] = 5;
  v19 = 0;
  sub_1406C8FF0(&v27);
  v10 = *(_QWORD *)(a1 + 32) + 176LL * (int)sub_1400AACE0(a1 + 24, v18);
  sub_1400AA9C0(&v32);
  sub_1400AA9C0(&v27);
  *(_BYTE *)(v10 + 40) = 0;
  *(_BYTE *)(sub_140C2D4F0(
               a1,
               (unsigned int)"SetPlayingID",
               (unsigned int)"music_player_nodes::sSetPlayingID",
               *(_QWORD *)(a1 + 16),
               (__int64)sub_140C167E0)
           + 40) = 0;
  *(_BYTE *)(sub_140C2D4F0(
               a1,
               (unsigned int)"SetPlayingPriorityMusic",
               (unsigned int)"music_player_nodes::sSetPlayingPriorityMusic",
               *(_QWORD *)(a1 + 16),
               (__int64)sub_140C16810)
           + 40) = 0;
  *(_BYTE *)(sub_140C2D7A0(a1, v11, v12, *(_QWORD *)(a1 + 16)) + 40) = 0;
  *(_BYTE *)(sub_140C2DA80(
               a1,
               (unsigned int)"RegisterBanRequestSimple",
               (unsigned int)"music_player_nodes::sRegisterBanRequestSimple",
               *(_QWORD *)(a1 + 16),
               (__int64)sub_140C16990)
           + 40) = 0;
  *(_BYTE *)(sub_140C2DCF0(a1, v13, v14, *(_QWORD *)(a1 + 16)) + 40) = 0;
  *(_BYTE *)(sub_1400F8410(
               a1,
               (unsigned int)"UnregisterBanRequest",
               (unsigned int)"music_player_nodes::sUnregisterBanRequest",
               *(_QWORD *)(a1 + 16),
               (__int64)sub_140C16B50)
           + 40) = 0;
  *(_BYTE *)(sub_140C2DFB0(a1, v15, v16, *(_QWORD *)(a1 + 16)) + 40) = 0;
  *(_BYTE *)(sub_140C2DA80(
               a1,
               (unsigned int)"SetIgnoreBanReason",
               (unsigned int)"music_player_nodes::sSetIgnoreBanReason",
               *(_QWORD *)(a1 + 16),
               (__int64)sub_140C16C40)
           + 40) = 0;
  *(_BYTE *)(sub_1400F8410(
               a1,
               (unsigned int)"ClearIgnoreBanReason",
               (unsigned int)"music_player_nodes::sClearIgnoreBanReason",
               *(_QWORD *)(a1 + 16),
               (__int64)sub_140C16C80)
           + 40) = 0;
  return sub_1400AAF30(a1 + 24);
}