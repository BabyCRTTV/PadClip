// Exercise the real window procedure and controls at simulated monitor DPIs.
// The hosted Windows display cannot physically switch to a Legion Go panel.
#include <windows.h>
#include <stdio.h>
static RECT testMonitor;
static BOOL WINAPI monitorInfo(HMONITOR monitor, LPMONITORINFO info) {
  BOOL result = GetMonitorInfoW(monitor, info);
  if (result && testMonitor.right)
    info->rcWork = info->rcMonitor = testMonitor;
  return result;
}

#define GetMonitorInfoW monitorInfo
#define wWinMain padClipMain
#include "../padclip.c"
#undef wWinMain

HRESULT captureClip(HWND window, HANDLE stop, wchar_t *out, size_t n, const wchar_t **step) {
  return E_NOTIMPL;
}

// Windows ordinarily caps the track size at the physical host desktop.
// Supply the same maximum a real monitor of this test size would provide.
static WNDPROC appProc;
static LRESULT CALLBACK testProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
  LRESULT result = CallWindowProcW(appProc, h, msg, w, l);
  if (msg == WM_GETMINMAXINFO && testMonitor.right) {
    MINMAXINFO *limits = (MINMAXINFO *)l;
    limits->ptMaxTrackSize.x = testMonitor.right;
    limits->ptMaxTrackSize.y = testMonitor.bottom;
  }
  return result;
}

static BOOL verifyLayout(UINT dpi, LONG width, LONG height) {
  testMonitor = (RECT){0, 0, width, height};
  RECT suggested = {0, 0, MulDiv(570, dpi, 96), MulDiv(450, dpi, 96)};
  SendMessageW(mainwin, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), (LPARAM)&suggested);
  RECT client, frame, update, first, last;
  GetWindowRect(mainwin, &frame);
  GetClientRect(mainwin, &client);
  GetWindowRect(updateButton, &update);
  MapWindowPoints(NULL, mainwin, (POINT *)&update, 2);
  GetWindowRect(change, &first);
  GetWindowRect(recordButton, &last);
  LOGFONTW font;
  HFONT controlFont = (HFONT)SendMessageW(startupBox, WM_GETFONT, 0, 0);
  BOOL ok = GetObjectW(controlFont, sizeof(font), &font) == sizeof(font) &&
            font.lfHeight == -MulDiv(10, dpi, 72) &&
            frame.right == suggested.right && frame.bottom == suggested.bottom &&
            frame.right <= width && frame.bottom <= height &&
            update.right <= client.right && update.bottom <= client.bottom &&
            first.top == last.top && first.right < last.left &&
            first.bottom - first.top >= MulDiv(33, dpi, 96);
  printf("%ldx%ld, %u DPI, client %ldx%ld: %s\n", width, height, dpi,
         client.right, client.bottom, ok ? "pass" : "fail");
  return ok;
}

static int result;
static VOID CALLBACK runCases(HWND window, UINT message, UINT_PTR id, DWORD time) {
  KillTimer(NULL, id);
  restore();
  appProc = (WNDPROC)SetWindowLongPtrW(mainwin, GWLP_WNDPROC, (LONG_PTR)testProc);
  result = !verifyLayout(144, 2560, 1600) || !verifyLayout(192, 2560, 1600) ||
           !verifyLayout(240, 2560, 1600) || !verifyLayout(192, 1600, 2560) ||
           !verifyLayout(96, 1920, 1080);
  exitPadClip();
}

int wmain(void) {
  if (!SetTimer(NULL, 0, 200, runCases))
    return 1;
  int status = padClipMain(GetModuleHandleW(NULL), NULL, L"--startup", SW_SHOWNORMAL);
  return status ? status : result;
}
