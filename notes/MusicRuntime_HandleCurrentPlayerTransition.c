char __fastcall MusicRuntime_HandleCurrentPlayerTransition(__int64 a1, __int64 a2, __int64 a3)
{
  __int64 v3; // rdi
  __int64 v4; // rax
  __int64 v5; // rcx
  int v6; // ecx
  int v7; // ebx
  int v8; // esi
  __int64 v9; // rbx
  int v10; // eax
  int CurrentSourcePositionSeconds; // eax
  volatile signed __int8 *v12; // rcx

  v3 = qword_7FF7BE0BEDA8;
  LOBYTE(v4) = *(_BYTE *)(qword_7FF7BE0BEDA8 + 6416);
  if ( (_BYTE)v4 != 3 )
  {
    if ( *(_QWORD *)(qword_7FF7BE0BEDA8 + 6424) )
    {
      LOBYTE(v4) = v4 - 2;
      if ( (v4 & 0xFB) == 0 )
      {
        CurrentSourcePositionSeconds = MusicRuntime_GetCurrentSourcePositionSeconds(qword_7FF7BE0BEDA8, a2, a3);
        DSMusicTelemetry_BeginWindow(CurrentSourcePositionSeconds);
        if ( *(_BYTE *)(v3 + 10296) )
        {
          LOBYTE(v4) = MusicRuntime_SetPlayStateAndNotify(v3, 5);
          *(_BYTE *)(v3 + 10296) = 0;
        }
        else
        {
          v12 = *(volatile signed __int8 **)(v3 + 6424);
          if ( *((_BYTE *)v12 + 698) )
            _InterlockedDecrement8(v12 + 698);
          if ( !*((_BYTE *)v12 + 698) )
            (*(void (__fastcall **)(volatile signed __int8 *, _QWORD, _QWORD))(*(_QWORD *)v12 + 280LL))(v12, 0, 0);
          *(_BYTE *)(*(_QWORD *)(v3 + 6424) + 698LL) = 0;
          LOBYTE(v4) = MusicRuntime_SetPlayStateAndNotify(v3, 1);
        }
      }
    }
    else
    {
      v5 = qword_7FF7BE0BEDA8;
      *(_DWORD *)(qword_7FF7BE0BEDA8 + 6436) = 0;
      LODWORD(v4) = MusicRuntime_GetCurrentSourcePositionSeconds(v5, a2, a3);
      v6 = *(_DWORD *)(v3 + 6436);
      v7 = v4;
      if ( v6 )
      {
        LOBYTE(v4) = DSMusicTelemetry_EndWindow(v6, v4);
        if ( *(_BYTE *)(v3 + 6416) == 1 )
        {
          if ( v7 - *(_DWORD *)(v3 + 10416) >= 60 )
          {
            LOBYTE(v4) = qword_7FF7BE0BECF8;
            v8 = *(_DWORD *)(v3 + 6436);
            v9 = *(_QWORD *)qword_7FF7BE0BECF8;
            if ( **(_BYTE **)qword_7FF7BE0BECF8 )
            {
              if ( *(_BYTE *)(v9 + 1) )
              {
                v4 = sub_7FF7B7F3DE00(v9 + 264, (unsigned int)(*(_DWORD *)(v9 + 264) + 1));
                *(_DWORD *)(v4 + 4LL * (int)(*(_DWORD *)(v9 + 264))++) = v8;
              }
            }
          }
          *(_DWORD *)(v3 + 10416) = 0;
        }
      }
      if ( *(_BYTE *)(v3 + 6416) )
      {
        v10 = *(_DWORD *)(v3 + 6436);
        if ( v10 && *(_DWORD *)(v3 + 6440) != v10 )
          *(_DWORD *)(v3 + 6440) = v10;
        *(_DWORD *)(v3 + 6436) = 0;
        MusicRuntime_SetPlayStateAndNotify(v3, 0);
        LOBYTE(v4) = MusicRuntime_ClearCurrentPlayerObject(v3);
        *(_WORD *)(v3 + 10276) = 0;
      }
    }
  }
  return v4;
}