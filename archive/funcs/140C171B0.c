__int64 __fastcall sub_140C171B0(__int64 a1)
{
  int v2; // edx

  sub_1400AB0E0(a1 + 24);
  sub_1400AB010(a1 + 24);
  LOBYTE(v2) = 2;
  *(_QWORD *)(a1 + 16) = "DSMusicPlayerTrackResourceSymbols";
  sub_1400A65F0(
    a1,
    v2,
    (unsigned int)&unk_144328810,
    (unsigned int)"DSMusicPlayerTrackResource",
    (__int64)"DSMusicPlayerTrackResource",
    0,
    0,
    0,
    0,
    1,
    0);
  sub_1400A6EE0(a1, (char **)&unk_144328810, (__int64)"UUIDRef_DSMusicPlayerTrackResource", 1);
  return sub_1400AAF30(a1 + 24);
}