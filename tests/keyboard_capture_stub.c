// Keep the real main window and shortcut event path, swapping only the encoder
// so a Windows runner without a hardware capture device can observe both
// toggles.
#include <windows.h>

HRESULT captureClip(HWND window, HANDLE stop, wchar_t *out, size_t n,
                    const wchar_t **stage) {
  (void)stage;
  PostMessageW(window, WM_APP + 1, TRUE, 0);
  WaitForSingleObject(stop, INFINITE);
  lstrcpynW(out, L"Keyboard test clip saved", (int)n);
  return S_OK;
}
