__int64 __fastcall AcquireRuntimeObjectAndEnableOnStart(__int64 a1)
{
  __int64 result; // rax
  __int64 v2; // rbx
  __int64 (__fastcall *v3)(__int64, __int16 *, int *); // r10
  int v4; // eax
  int v5; // r8d
  unsigned int v6; // eax
  unsigned __int64 *v7; // rcx
  unsigned __int64 v8; // rdx
  unsigned int v9; // eax
  char v10; // [rsp+38h] [rbp+10h] BYREF
  int v11; // [rsp+40h] [rbp+18h] BYREF
  unsigned __int64 *v12; // [rsp+48h] [rbp+20h] BYREF

  result = sub_7FF7F57AA880(qword_7FF7FD2C1220, 0, a1, 0);
  v2 = result;
  if ( result )
  {
    v12 = 0;
    sub_7FF7F31C3FC0((int **)&v12, "_on_start_", 0xAu);
    v3 = *(__int64 (__fastcall **)(__int64, __int16 *, int *))(*(_QWORD *)v2 + 104LL);
    v4 = *((_DWORD *)v12 - 3);
    if ( v4 == -1 )
    {
      v5 = *((_DWORD *)v12 - 2);
      v6 = 0;
      v7 = v12;
      if ( v5 >= 8 )
      {
        v8 = (unsigned __int64)(unsigned int)v5 >> 3;
        do
        {
          v6 = _mm_crc32_u64(v6, *v7++);
          --v8;
        }
        while ( v8 );
      }
      if ( (v5 & 4) != 0 )
      {
        v6 = _mm_crc32_u32(v6, *(_DWORD *)v7);
        v7 = (unsigned __int64 *)((char *)v7 + 4);
      }
      if ( (v5 & 2) != 0 )
      {
        v6 = _mm_crc32_u16(v6, *(_WORD *)v7);
        v7 = (unsigned __int64 *)((char *)v7 + 2);
      }
      if ( (v5 & 1) != 0 )
        v6 = _mm_crc32_u8(v6, *(_BYTE *)v7);
      v4 = v6 & 0x7FFFFFFF;
      *((_DWORD *)v12 - 3) = v4;
    }
    v11 = v4;
    v9 = v3(v2, &word_7FF7F6C63AE0, &v11);
    v10 = 1;
    (*(void (__fastcall **)(__int64, _QWORD, char *))(*(_QWORD *)v2 + 80LL))(v2, v9, &v10);
    sub_7FF7F31C3760(&v12);
    return v2;
  }
  return result;
}