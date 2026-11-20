#pragma once
#include <windows.h>
BOOL beginUpdate(HWND owner);
BOOL updateReady(void);
BOOL updateFinished(DWORD *exitCode);
void closeUpdateHandles(void);
void showReleaseNotes(HWND owner, BOOL afterUpdate);
