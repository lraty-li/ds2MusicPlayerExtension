__int64 __fastcall MusicRuntime_SetPlayStateAndNotify(__int64 a1, char a2)
{
  __int64 result; // rax
  __int64 v4; // r14
  int v5; // ebx
  __int64 v6; // rax
  __int64 i; // rcx
  __int64 v8; // rsi
  __int64 *v9; // rcx
  __int64 v10; // rax
  __int64 *v11; // rcx
  __int64 v12; // rax
  __int64 v13; // rax
  __int64 v14; // rdx
  char v15; // cl
  char v16; // si
  char v17; // bl
  char *v18; // r8
  char *v19; // r8
  char *v20; // r8

  result = *(unsigned __int8 *)(a1 + 6416);
  if ( (_BYTE)result != a2 )
  {
    if ( (_BYTE)result == 5 )
      *(_BYTE *)(a1 + 10422) = 0;
    *(_BYTE *)(a1 + 6416) = a2;
    if ( a2 == 5 )
    {
      *(_DWORD *)(a1 + 10264) = 0;
    }
    else if ( !a2 )
    {
      v4 = *(int *)(a1 + 10404);
      v5 = 0;
      if ( (int)v4 > 0 )
      {
        v6 = *(_QWORD *)(a1 + 10392);
        for ( i = 0; i < v4; ++i )
        {
          if ( *(_DWORD *)(v6 + 48) )
            break;
          ++v5;
          v6 += 56;
        }
      }
      while ( v5 != (_DWORD)v4 )
      {
        v8 = *(_QWORD *)(a1 + 10392) + 56LL * v5;
        v9 = *(__int64 **)(v8 + 24);
        if ( v9 )
        {
          v10 = *v9;
          *((_BYTE *)v9 + 384) &= ~4u;
          (*(void (__fastcall **)(__int64 *, _QWORD))(v10 + 272))(v9, 0);
          ReleaseRuntimeObject(*(__int64 **)(v8 + 24));
          *(_QWORD *)(v8 + 24) = 0;
        }
        v11 = *(__int64 **)(v8 + 32);
        if ( v11 )
        {
          v12 = *v11;
          *((_BYTE *)v11 + 384) &= ~4u;
          (*(void (__fastcall **)(__int64 *, _QWORD))(v12 + 272))(v11, 0);
          ReleaseRuntimeObject(*(__int64 **)(v8 + 32));
          *(_QWORD *)(v8 + 32) = 0;
        }
        do
          v13 = v5++;
        while ( v5 < *(_DWORD *)(a1 + 10404) && !*(_DWORD *)(56 * v13 + *(_QWORD *)(a1 + 10392) + 104) );
      }
    }
    v14 = qword_7FF7B9CAED88;
    if ( qword_7FF7B9CAED88 )
    {
      v15 = *(_BYTE *)(a1 + 6416);
      if ( (unsigned __int8)(v15 - 3) <= 1u )
      {
        v16 = 1;
        v17 = ((v15 - 1) & 0xFB) == 0;
        if ( ((*(_BYTE *)(a1 + 10276) - 1) & 0xFB) == 0 )
          v17 = 1;
      }
      else
      {
        v16 = 0;
        v17 = ((v15 - 1) & 0xFB) == 0;
      }
      v18 = *(char **)(qword_7FF7B9CAED88 + 136);
      if ( v18 )
      {
        DispatchFactChangeToListener(qword_7FF7B9CB7A68, (__int64)&xmmword_7FF7B6E6D360, v18, ((v15 - 1) & 0xFB) == 0);
        v14 = qword_7FF7B9CAED88;
      }
      v19 = *(char **)(v14 + 144);
      if ( v19 )
      {
        DispatchFactChangeToListener(qword_7FF7B9CB7A68, (__int64)&xmmword_7FF7B6E6D360, v19, v16);
        v14 = qword_7FF7B9CAED88;
      }
      v20 = *(char **)(v14 + 152);
      if ( v20 )
        DispatchFactChangeToListener(qword_7FF7B9CB7A68, (__int64)&xmmword_7FF7B6E6D360, v20, v17);
    }
    result = (__int64)qword_7FF7B9CAED70;
    if ( qword_7FF7B9CAED70 )
      return qword_7FF7B9CAED70();
  }
  return result;
}