#pragma once

#include <windows.h>

namespace TextSharedStringResolver
{
using LocalizedTextToUiSharedStringFn =
    void* (__fastcall*)(void* localizedText, void** outSlot);
using UiSharedStringMoveAssignFn = void* (__fastcall*)(void** target, void** source);

bool Resolve(HMODULE gameModule,
    LocalizedTextToUiSharedStringFn& toUiSharedString,
    UiSharedStringMoveAssignFn& moveAssign);
}
