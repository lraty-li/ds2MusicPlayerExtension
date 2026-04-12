__int64 __fastcall sub_1426C3B10(__int64 a1, __int64 a2, wchar_t *outPath, wchar_t *basePathOpt)
{
    wchar_t *name = *(wchar_t **)a2;
    unsigned __int64 baseLen = 0;
    unsigned __int64 nameLen;
    __int64 copiedLen = 0;
    char baseOk = 1;
    char appendOk;
    wchar_t tmpName[16];

    // 文件名和 fileId 必须二选一
    if ( (*(_QWORD *)a2 != 0) == (*(_DWORD *)(a2 + 8) != -1) )
        return 31;

    // 可选基础目录必须以 '\' 结尾
    if (basePathOpt) {
        while (basePathOpt[baseLen]) ++baseLen;
        if (baseLen && basePathOpt[baseLen - 1] != L'\\')
            return 31;
    }

    // 没有直接文件名时，按 fileId 生成 "%u.wem" / "%u.bnk"
    if (!name) {
        _DWORD *info = *(_DWORD **)(a2 + 12);
        if (!info || *info > 1u)
            return 31;

        if ((info[1] - 1 > 0x1C) && (info[1] < 0x20))
            swprintf_0(tmpName, 0xF, L"%u.bnk");
        else
            swprintf_0(tmpName, 0xF, L"%u.wem");

        name = tmpName;
    }

    // 计算文件名长度
    nameLen = 0;
    while (name[nameLen]) ++nameLen;

    // 绝对路径：直接复制
    if (*(_QWORD *)a2) {
        if ((nameLen >= 3 && name[1] == L':' && name[2] == L'\\') ||
            (nameLen >= 2 && name[0] == L'\\'))
        {
            sub_1426C3AA0(outPath, name, 260);
            for (wchar_t *p = outPath; *p; ++p)
                if (*p == L'/') *p = L'\\';
            return 1;
        }
    }

    // 相对路径：先拷基础目录
    *outPath = 0;
    if (basePathOpt) {
        if (baseLen < 0x104) {
            memcpy(outPath, basePathOpt, 2 * baseLen);
            outPath[baseLen] = 0;
            copiedLen = baseLen;
        } else {
            baseOk = 0;
        }
    } else {
        __int64 defaultBase = *(_QWORD *)(a1 + 8);
        if (defaultBase) {
            wchar_t *defaultPath = (wchar_t *)(defaultBase + 8);
            unsigned __int64 len = 0;
            while (defaultPath[len]) ++len;
            if (len < 0x104) {
                memcpy(outPath, defaultPath, 2 * len);
                outPath[len] = 0;
                copiedLen = len;
            } else {
                baseOk = 0;
            }
        }
    }

    // 追加文件名，或走复杂拼接分支
    if (!*(_QWORD *)(a2 + 12) || *(_DWORD *)(a2 + 20)) {
        if (copiedLen + nameLen < 0x104) {
            memcpy(&outPath[copiedLen], name, 2 * nameLen);
            outPath[copiedLen + nameLen] = 0;
            appendOk = 1;
        } else {
            appendOk = 0;
        }
    } else {
        appendOk = sub_1426C66D0(/*builder*/, name, *(_QWORD *)(a2 + 12), *(unsigned __int8 *)(a1 + 16));
    }

    if (!(baseOk & appendOk))
        return 108;

    for (wchar_t *p = outPath; *p; ++p)
        if (*p == L'/') *p = L'\\';

    return 1;
}
