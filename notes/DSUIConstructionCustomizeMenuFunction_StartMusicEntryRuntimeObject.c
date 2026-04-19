void __fastcall DSUIConstructionCustomizeMenuFunction_StartMusicEntryRuntimeObject(__int64 a1, __int64 a2, char a3)
{
  __int64 v6; // rbp
  __int64 v7; // rcx
  __int64 v8; // rcx
  __int64 v9; // rax

  if ( a2 && *(_QWORD *)(a2 + 48) )
  {
    v6 = sub_7FF7F47313F0();
    if ( *(_QWORD *)(a1 + 216) )
      DSUIConstructionCustomizeMenuFunction_StopCurrentMusicPlaying(a1, 1);
    v7 = *(_QWORD *)(a2 + 48);
    if ( a3 )
    {
      v8 = *(_QWORD *)(v7 + 72);
      *(_BYTE *)(a2 + 42) = 1;
      *(_BYTE *)(v6 + 32) |= 1u;
    }
    else
    {
      v8 = *(_QWORD *)(v7 + 64);
    }
    v9 = AcquireRuntimeObjectAndEnableOnStart(v8);
    *(_QWORD *)(a1 + 216) = v9;
    if ( v9 )
    {
      (*(void (__fastcall **)(__int64))(*(_QWORD *)v9 + 144LL))(v9);
      *(_BYTE *)(a1 + 224) = a3;
    }
  }
}