void __fastcall DispatchFactChangeToListener(__int64 a1, __int64 a2, char *a3, char a4)
{
  RTL_SRWLOCK *v4; // rbx
  int v8; // eax
  unsigned int v9; // eax
  __int64 v10; // r9
  PSRWLOCK SRWLock; // [rsp+40h] [rbp-38h] BYREF
  int v12; // [rsp+48h] [rbp-30h]
  char v13; // [rsp+80h] [rbp+8h] BYREF
  char v14; // [rsp+90h] [rbp+18h] BYREF
  char v15; // [rsp+98h] [rbp+20h] BYREF

  v4 = (RTL_SRWLOCK *)(a1 + 8);
  v15 = a4;
  v13 = 0;
  SRWLock = (PSRWLOCK)(a1 + 8);
  v12 = 0;
  if ( !TryAcquireSRWLockShared((PSRWLOCK)(a1 + 8)) )
    AcquireSRWLockShared(v4);
  v8 = sub_7FF615747EA0(a1, a2, &SRWLock);
  v14 = a3[40];
  sub_7FF6136090B0(v8, (_DWORD)a3 + 16, (unsigned int)&v15, (unsigned int)&v14, a3[32], a3[33], (__int64)&v13);
  if ( v12 )
  {
    if ( v12 == 1 )
      ReleaseSRWLockExclusive(SRWLock);
  }
  else
  {
    ReleaseSRWLockShared(SRWLock);
  }
  if ( v13 )
  {
    if ( !TryEnterCriticalSection((LPCRITICAL_SECTION)(a1 + 48)) )
      EnterCriticalSection((LPCRITICAL_SECTION)(a1 + 48));
    v9 = sub_7FF613617310(a1 + 104, a2);
    if ( v9 != -1 && v9 < *(_DWORD *)(a1 + 116) )
    {
      v10 = *(_QWORD *)(a1 + 104) + 40LL * (int)v9;
      if ( *(_DWORD *)(v10 + 32) )
      // __int64 __fastcall sub_7FF6157459A0(__int64 this_, __int64 a2, char *a3, int *a4)
        (*(void (__fastcall **)(char *, __int64, char *, __int64))(*(_QWORD *)a3 + 96LL))(a3, a2, &v15, v10 + 16);
    }
    if ( *(_DWORD *)(a1 + 88) )
      (*(void (__fastcall **)(char *, __int64, char *, __int64))(*(_QWORD *)a3 + 96LL))(a3, a2, &v15, a1 + 88);
    if ( a1 != -48 )
      LeaveCriticalSection((LPCRITICAL_SECTION)(a1 + 48));
  }
}