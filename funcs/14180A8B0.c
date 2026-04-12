__int64 __fastcall sub_14180A8B0(__int64 a1)
{
  int v2; // edx
  int v3; // r9d
  int v4; // r9d
  int v5; // r9d
  int v6; // r9d
  int v7; // r9d
  int v8; // r9d
  int v9; // r9d
  int v10; // r9d
  int v11; // r9d
  int v12; // r9d
  int v13; // r9d
  int v14; // r9d
  int v15; // r9d
  int v16; // r9d
  int v17; // r9d
  int v18; // r9d
  int v19; // r9d
  int v20; // r9d
  int v21; // r9d
  int v22; // r9d
  int v23; // r9d
  int v24; // r9d
  int v25; // r9d
  int v26; // r9d
  int v27; // r9d
  int v28; // r9d
  int v29; // r9d
  int v30; // r9d
  int v31; // r9d
  int v32; // r9d

  sub_1400AB0E0(a1 + 24);
  sub_1400AB010(a1 + 24);
  LOBYTE(v2) = 2;
  *(_QWORD *)(a1 + 16) = "DSUIMusicMenuFunctionSymbols";
  sub_1400A65F0(
    a1,
    v2,
    (unsigned int)&unk_144408280,
    (unsigned int)"DSUIMusicMenuFunction",
    (__int64)"DSUIMusicMenuFunction",
    0,
    0,
    0,
    0,
    1,
    0);
  sub_1400A6EE0(a1, (char **)&unk_144408280, (__int64)"UUIDRef_DSUIMusicMenuFunction", 1);
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnMouseAcceptOnRepeatButton",
               (unsigned int)"DSUIMusicMenuFunction_OnMouseAcceptOnRepeatButton",
               v3,
               (__int64)sub_14180F390)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnMouseAcceptOnShuffleButton",
               (unsigned int)"DSUIMusicMenuFunction_OnMouseAcceptOnShuffleButton",
               v4,
               (__int64)sub_14180F3A0)
           + 40) = 0;
  *(_BYTE *)(sub_14180EEE0(
               a1,
               (unsigned int)"OnMouseOverListItem",
               (unsigned int)"DSUIMusicMenuFunction_OnMouseOverListItem",
               v5,
               (__int64)sub_14180F3B0)
           + 40) = 0;
  *(_BYTE *)(sub_14180EEE0(
               a1,
               (unsigned int)"OnMouseAcceptOnListItem",
               (unsigned int)"DSUIMusicMenuFunction_OnMouseAcceptOnListItem",
               v6,
               (__int64)sub_14180F3C0)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnChangePlaylistAfterAnimation",
               (unsigned int)"DSUIMusicMenuFunction_OnChangePlaylistAfterAnimation",
               v7,
               (__int64)sub_14180F480)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnChangePlaylistStartAnimation",
               (unsigned int)"DSUIMusicMenuFunction_OnChangePlaylistStartAnimation",
               v8,
               (__int64)sub_14180F490)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnResetPlaylistAfterAnimation",
               (unsigned int)"DSUIMusicMenuFunction_OnResetPlaylistAfterAnimation",
               v9,
               (__int64)sub_14180F4A0)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnResetCursor",
               (unsigned int)"DSUIMusicMenuFunction_OnResetCursor",
               v10,
               (__int64)sub_14180F4B0)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnChangePlaylist",
               (unsigned int)"DSUIMusicMenuFunction_OnChangePlaylist",
               v11,
               (__int64)sub_14180F4C0)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnSetupApasPlaylist",
               (unsigned int)"DSUIMusicMenuFunction_OnSetupApasPlaylist",
               v12,
               (__int64)sub_14180F4D0)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnPressIntel",
               (unsigned int)"DSUIMusicMenuFunction_OnPressIntel",
               v13,
               (__int64)sub_14180F4E0)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnPressDownMusicVolume",
               (unsigned int)"DSUIMusicMenuFunction_OnPressDownMusicVolume",
               v14,
               (__int64)sub_14180F4F0)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnPressUpMusicVolume",
               (unsigned int)"DSUIMusicMenuFunction_OnPressUpMusicVolume",
               v15,
               (__int64)sub_14180F500)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnPlayPrevMusic",
               (unsigned int)"DSUIMusicMenuFunction_OnPlayPrevMusic",
               v16,
               (__int64)sub_14180F510)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnPlayNextMusic",
               (unsigned int)"DSUIMusicMenuFunction_OnPlayNextMusic",
               v17,
               (__int64)sub_14180F520)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnRightAndAcceptMusicListItem",
               (unsigned int)"DSUIMusicMenuFunction_OnRightAndAcceptMusicListItem",
               v18,
               (__int64)sub_14180F530)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnLeftAndAcceptMusicListItem",
               (unsigned int)"DSUIMusicMenuFunction_OnLeftAndAcceptMusicListItem",
               v19,
               (__int64)sub_14180F540)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnDownAndAcceptMusicListItem",
               (unsigned int)"DSUIMusicMenuFunction_OnDownAndAcceptMusicListItem",
               v20,
               (__int64)sub_14180F550)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnUpAndAcceptMusicListItem",
               (unsigned int)"DSUIMusicMenuFunction_OnUpAndAcceptMusicListItem",
               v21,
               (__int64)sub_14180F560)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnDpadInpuMusicListItem",
               (unsigned int)"DSUIMusicMenuFunction_OnDpadInpuMusicListItem",
               v22,
               (__int64)sub_1414A4DC0)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnUnFocusListItem",
               (unsigned int)"DSUIMusicMenuFunction_OnUnFocusListItem",
               v23,
               (__int64)sub_14180F570)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnFocusListItem",
               (unsigned int)"DSUIMusicMenuFunction_OnFocusListItem",
               v24,
               (__int64)sub_14180F580)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnReleaseOptions",
               (unsigned int)"DSUIMusicMenuFunction_OnReleaseOptions",
               v25,
               (__int64)AK::MemoryMgr::StartProfileThreadUsage)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnPressOptions",
               (unsigned int)"DSUIMusicMenuFunction_OnPressOptions",
               v26,
               (__int64)sub_14180F590)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnPressInbox",
               (unsigned int)"DSUIMusicMenuFunction_OnPressInbox",
               v27,
               (__int64)sub_14180F5A0)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnReleaseAccept",
               (unsigned int)"DSUIMusicMenuFunction_OnReleaseAccept",
               v28,
               (__int64)sub_14180F5B0)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnPressAccept",
               (unsigned int)"DSUIMusicMenuFunction_OnPressAccept",
               v29,
               (__int64)sub_14180F5C0)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnCancelMusicMenu",
               (unsigned int)"DSUIMusicMenuFunction_OnCancelMusicMenu",
               v30,
               (__int64)sub_14180F5D0)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnFocusMusicMenu",
               (unsigned int)"DSUIMusicMenuFunction_OnFocusMusicMenu",
               v31,
               (__int64)AK::MemoryMgr::StartProfileThreadUsage)
           + 40) = 0;
  *(_BYTE *)(sub_14180ECF0(
               a1,
               (unsigned int)"OnPageOn",
               (unsigned int)"DSUIMusicMenuFunction_OnPageOn",
               v32,
               (__int64)AK::MemoryMgr::StartProfileThreadUsage)
           + 40) = 0;
  return sub_1400AAF30(a1 + 24);
}