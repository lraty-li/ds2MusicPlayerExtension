__int64 __fastcall DSUIMusicMenu_HandleTogglePlayback(__int64 a1)
{
  __int64 v1; // rbp
  __int64 v2; // rbx
  int Index; // eax
  __int64 v4; // rdx
  __int64 v5; // r8
  __int16 v6; // ax
  bool v7; // zf
  char v8; // bl
  __int64 v9; // rdx
  __int64 v10; // rdx
  __int64 v11; // rcx
  __int64 v12; // r8
  __int64 v13; // r13
  __int64 v14; // rcx
  __int64 *v15; // r14
  __int64 *v16; // r12
  __int64 v17; // r15
  __int64 v18; // rbx
  __int64 v19; // rdi
  __int64 v20; // rdx
  __int64 v21; // rsi
  unsigned __int64 v22; // rdi
  __int64 v23; // rbx
  __int64 v24; // r8
  __int64 v25; // rsi
  __int64 i; // rbx
  __int64 v27; // rcx
  __int64 (__fastcall ***v28)(); // r8
  __int64 (__fastcall ***v29)(); // rcx
  __int64 v30; // rax
  __int64 (__fastcall **v31)(); // rax
  __int64 v32; // rdx
  _DWORD *v33; // rcx
  __int64 result; // rax
  __int64 v35; // [rsp+30h] [rbp-68h] BYREF
  unsigned __int64 v36; // [rsp+38h] [rbp-60h]
  __int64 v37; // [rsp+40h] [rbp-58h] BYREF
  __int64 *v38; // [rsp+48h] [rbp-50h]
  __int64 v39; // [rsp+50h] [rbp-48h] BYREF
  int v40; // [rsp+58h] [rbp-40h]
  __int64 v42; // [rsp+A8h] [rbp+10h] BYREF

  v1 = a1;
  LOBYTE(v42) = 25;
  v2 = *(_QWORD *)qword_7FF7BE0BF308;
  Index = ByteKeyHashTable_FindIndex((__int64 *)(*(_QWORD *)qword_7FF7BE0BF308 + 6560LL), (unsigned __int8 *)&v42);
  if ( Index == -1 )
    v42 = 0;
  else
    v42 = *(_QWORD *)(*(_QWORD *)(v2 + 6560) + 24LL * Index);
  (*(void (__fastcall **)(_QWORD))(**(_QWORD **)(v1 + 232) + 200LL))(*(_QWORD *)(v1 + 232));
  v6 = *(_WORD *)(qword_7FF7BE0BEDA8 + 6418);
  v7 = v6 == 0;
  if ( v6 )
    goto LABEL_43;
  if ( *(int *)(qword_7FF7BE0BEDA8 + 6472) <= 0 )
  {
    v7 = 1;
LABEL_43:
    DSUIMusicMenu_UpdateUnavailableActionHint(v1, v7 + 2);
    return DSUIController_UpdateCurrentSelectionContextText(v1);
  }
  v8 = *(_BYTE *)(qword_7FF7BE0BEDA8 + 6416);
  if ( v8 != 1 )
  {
    if ( *(int *)(MusicRuntime_FindCurrentEntryByTrackId(qword_7FF7BE0BEDA8) + 48) < 0 || ((v8 - 2) & 0xFB) != 0 )
      MusicRuntime_ApplyCurrentPlayOrderEntry(v11, v10, v12);
    else
      MusicRuntime_HandleCurrentPlayerTransition();
    v13 = *(_QWORD *)(v1 + 88);
    v37 = 0;
    v38 = 0;
    v14 = *(_QWORD *)(qword_7FF7BE0C7A98 + 208);
    *(_QWORD *)(v14 + 64) = *(_QWORD *)(v13 + 192);
    *(_BYTE *)(v14 + 72) = 1;
    v35 = *(_QWORD *)(v13 + 136);
    LODWORD(v36) = *(_DWORD *)(v13 + 128);
    RefCountedPtrArray_Assign(&v37, &v35);
    v15 = v38;
    v16 = &v38[(int)v37];
    if ( v38 == v16 )
    {
LABEL_37:
      RefCountedPtrArray_Destroy(&v37);
      goto LABEL_38;
    }
    v17 = 0;
    while ( 1 )
    {
      v18 = *v15;
      v39 = *(_QWORD *)(v13 + 160);
      v40 = *(_DWORD *)(v13 + 152);
      v35 = 0;
      v36 = 0;
      RefCountedPtrArray_Assign(&v35, &v39);
      v19 = qword_7FF7BE0C7A98;
      v21 = CachedTree_FindNodeByResolvedKeyPair(*(_QWORD *)(qword_7FF7BE0C7A98 + 160), v18);
      if ( !v21 )
        v21 = ObjectArray_FindFirstQueryResult(*(_QWORD *)(v19 + 176), v18);
      v22 = v36;
      v23 = *(_QWORD *)(v36 + v17);
      v17 += 8;
      if ( v23 )
      {
        _InterlockedIncrement((volatile signed __int32 *)(v23 + 8));
        if ( v21 )
        {
          LOBYTE(v20) = 1;
          TaggableObject_RequestShownState(v21, v20);
          LOBYTE(v24) = 1;
          Widget_EnqueueAnimationEvent(v21, v23, v24);
        }
        RefCountedObject_Release(v23);
        v22 = v36;
      }
      v25 = (int)v35;
      for ( i = 0; i < v25; ++i )
      {
        v27 = *(_QWORD *)(v22 + 8 * i);
        if ( v27 )
          RefCountedObject_Release(v27);
      }
      if ( !v22 )
        goto LABEL_35;
      v28 = (__int64 (__fastcall ***)())qword_7FF7C2039128;
      if ( v22 < (unsigned __int64)xmmword_7FF7BC0420C0 || v22 >= *((_QWORD *)&xmmword_7FF7BC0420C0 + 1) )
      {
        v30 = *(_QWORD *)NtCurrentTeb()->ThreadLocalStoragePointer;
        v29 = *(__int64 (__fastcall ****)())(v30 + 6760);
        if ( !v29 )
        {
          *(_QWORD *)(v30 + 6760) = qword_7FF7C2039128;
          v29 = v28;
        }
        if ( v29 == &off_7FF7BC0413A0 )
        {
LABEL_33:
          v31 = *v28;
          v29 = v28;
          goto LABEL_34;
        }
      }
      else
      {
        v29 = &off_7FF7BC0413A0;
      }
      if ( v29 == v28 )
        goto LABEL_33;
      v31 = *v29;
LABEL_34:
      ((void (__fastcall *)(__int64 (__fastcall ***)(), unsigned __int64))v31[18])(v29, v22);
LABEL_35:
      if ( ++v15 == v16 )
      {
        v1 = a1;
        goto LABEL_37;
      }
    }
  }
  MusicRuntime_HandlePauseRequest(qword_7FF7BE0BEDA8, v4, v5);
  v9 = *(_QWORD *)(qword_7FF7BE0C7A98 + 208);
  *(_QWORD *)(v9 + 64) = *(_QWORD *)(*(_QWORD *)(v1 + 88) + 200LL);
  *(_BYTE *)(v9 + 72) = 1;
LABEL_38:
  v32 = *(unsigned int *)(qword_7FF7BE0BEDA8 + 6448);
  if ( (int)v32 >= 0 )
  {
    v33 = *(_DWORD **)(v1 + 240);
    if ( (int)v32 < v33[8] )
      (*(void (__fastcall **)(_DWORD *, __int64, _QWORD, _QWORD, _BYTE))(*(_QWORD *)v33 + 192LL))(v33, v32, 0, 0, 0);
  }
  result = v42;
  *(_BYTE *)(v42 + 32) |= 7u;
  return result;
}