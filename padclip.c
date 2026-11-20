#define _UNICODE
#include <shlobj.h>
#include <strsafe.h>
#include <windows.h>
#include <xinput.h>
#include "updates.h"

static HWND mainwin, binding, status, change, updateButton;
static const wchar_t *buttons[] = {L"D-pad Up",
                                   L"D-pad Down",
                                   L"D-pad Left",
                                   L"D-pad Right",
                                   L"Menu",
                                   L"View",
                                   L"LS click",
                                   L"RS click",
                                   L"LB",
                                   L"RB",
                                   0,
                                   0,
                                   L"A",
                                   L"B",
                                   L"X",
                                   L"Y",
                                   L"LT",
                                   L"RT",
                                   L"LS Left",
                                   L"LS Right",
                                   L"LS Down",
                                   L"LS Up",
                                   L"RS Left",
                                   L"RS Right",
                                   L"RS Down",
                                   L"RS Up"};
static DWORD shortcut =
                 XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER,
             candidate;
static BOOL learning, armed, latched[4];
static int owner = -1;
static const wchar_t config[] = L"Software\\8BitTrade\\PadClip";
static BOOL trayMode, closeToTray, inTray;
enum { TRAY_OPEN = 100, TRAY_EXIT };
static UINT explorerRestart;
static HICON smallAppIcon;
static HWND trayBox, closeTrayBox, startupBox;
static const wchar_t runKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static BOOL trayIcon(DWORD action) {
  NOTIFYICONDATAW icon = {0};
  icon.cbSize = sizeof(icon);
  icon.hWnd = mainwin;
  icon.uID = 1;
  icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  icon.uCallbackMessage = WM_APP + 3;
  icon.hIcon = smallAppIcon;
  StringCchCopyW(icon.szTip, 128, L"PadClip");
  return Shell_NotifyIconW(action, &icon);
}
static void restore(void) {
  if (inTray) {
    trayIcon(NIM_DELETE);
    inTray = FALSE;
  }
  ShowWindow(mainwin, SW_RESTORE);
  SetForegroundWindow(mainwin);
}
static void minimize(BOOL useTray) {
  if (useTray && (inTray || trayIcon(NIM_ADD))) {
    inTray = TRUE;
    ShowWindow(mainwin, SW_HIDE);
  } else
    ShowWindow(mainwin, SW_MINIMIZE);
}
static BOOL readTrayPreference(const wchar_t *name) {
  DWORD value = 0, size = sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, config, name, RRF_RT_REG_DWORD, NULL,
                      &value, &size) == ERROR_SUCCESS && value != 0;
}
static void saveTrayPreference(HWND box, const wchar_t *name, BOOL *setting) {
  DWORD value = SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
  if (RegSetKeyValueW(HKEY_CURRENT_USER, config, name, REG_DWORD, &value,
                      sizeof(value)) == ERROR_SUCCESS) {
    *setting = value;
  } else {
    SendMessageW(box, BM_SETCHECK, *setting, 0);
    MessageBoxW(mainwin, L"Could not save the system tray preference.", L"PadClip",
                MB_ICONWARNING);
  }
}
static BOOL startupEnabled(void) {
  DWORD size = 0;
  return RegGetValueW(HKEY_CURRENT_USER, runKey, L"PadClip", RRF_RT_REG_SZ,
                      NULL, NULL, &size) == ERROR_SUCCESS;
}
static BOOL setStartup(BOOL enabled) {
  if (!enabled) {
    LSTATUS result = RegDeleteKeyValueW(HKEY_CURRENT_USER, runKey, L"PadClip");
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
  }
  wchar_t exe[MAX_PATH], cmd[MAX_PATH + 20];
  DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
  if (!n || n >= MAX_PATH ||
      FAILED(StringCchPrintfW(cmd, MAX_PATH + 20, L"\"%ls\" --startup", exe)))
    return FALSE;
  return RegSetKeyValueW(HKEY_CURRENT_USER, runKey, L"PadClip", REG_SZ, cmd,
                         (lstrlenW(cmd) + 1) * sizeof(wchar_t)) ==
         ERROR_SUCCESS;
}
static void show(HWND h, const wchar_t *s) {
  wchar_t old[512];
  GetWindowTextW(h, old, 512);
  if (lstrcmpW(old, s))
    SetWindowTextW(h, s);
}
static void describe(HWND h, const wchar_t *prefix, DWORD value) {
  wchar_t text[512];
  StringCchCopyW(text, 512, prefix);
  BOOL first = TRUE;
  for (unsigned i = 0; i < 26; i++)
    if (buttons[i] && (value & (1u << i))) {
      if (!first)
        StringCchCatW(text, 512, L" + ");
      StringCchCatW(text, 512, buttons[i]);
      first = FALSE;
    }
  show(h, text);
}
static DWORD inputs(XINPUT_GAMEPAD p) {
  DWORD m = p.wButtons & 0xf3ff;
  BOOL extra[] = {p.bLeftTrigger > 120, p.bRightTrigger > 120,
                  p.sThumbLX < -22000,  p.sThumbLX > 22000,
                  p.sThumbLY < -22000,  p.sThumbLY > 22000,
                  p.sThumbRX < -22000,  p.sThumbRX > 22000,
                  p.sThumbRY < -22000,  p.sThumbRY > 22000};
  for (unsigned i = 0; i < 10; i++)
    if (extra[i])
      m |= 1u << (16 + i);
  return m;
}
static void finish(void) {
  learning = FALSE;
  EnableWindow(updateButton, TRUE);
  owner = -1;
  candidate = 0;
  show(change, L"Change shortcut");
  describe(binding, L"Recording shortcut     ", shortcut);
}
extern HRESULT captureClip(HWND, HANDLE, wchar_t *, size_t, const wchar_t **);
static HWND notice, details, recordButton;
static HANDLE stopEvent, worker;
static BOOL closing, stopping, updating;

static wchar_t path[MAX_PATH];
static const wchar_t *stage;
static int activePad = -1;
static void notify(const wchar_t *message) {
  show(details, message);
  show(notice, message);
  RECT area;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
  SetWindowPos(notice, HWND_TOPMOST, area.right - 380, area.top + 18, 360, 78,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  SetTimer(mainwin, 2, 3600, NULL);
  if (activePad >= 0) {
    XINPUT_VIBRATION v = {16000, 22000};
    XInputSetState(activePad, &v);
    SetTimer(mainwin, 3, 160, NULL);
  }
}
static DWORD WINAPI runCapture(void *unused) {
  (void)unused;
  HRESULT hr = captureClip(mainwin, stopEvent, path, MAX_PATH, &stage);
  PostMessageW(mainwin, WM_APP + 2, (WPARAM)(DWORD)hr, 0);
  return 0;
}
static void toggle(void) {
  if (updating || closing)
    return;
  if (worker) {
    if (!stopping) {
      stopping = TRUE;
      SetEvent(stopEvent);
      show(details, L"Saving clip...");
    }
    return;
  }
  stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
  path[0] = 0;
  stage = L"starting recorder";
  if (stopEvent)
    worker = CreateThread(NULL, 0, runCapture, NULL, 0, NULL);
  if (!worker) {
    if (stopEvent)
      CloseHandle(stopEvent);
    notify(L"Could not start the recorder.");
    return;
  }
  stopping = FALSE;
  EnableWindow(updateButton, FALSE);
  show(details, L"Starting recorder...");
  show(recordButton, L"Stop and save");
  EnableWindow(change, FALSE);
}
static void poll(void) {
  DWORD m[4] = {0};
  BOOL connected[4] = {0}, any = FALSE, neutral = TRUE;
  static ULONGLONG nextProbe[4];
  ULONGLONG now = GetTickCount64();
  for (int i = 0; i < 4; i++) {
    if (now < nextProbe[i])
      continue;
    XINPUT_STATE state;
    if (XInputGetState(i, &state) == ERROR_SUCCESS) {
      any = connected[i] = TRUE;
      m[i] = inputs(state.Gamepad);
      if (m[i])
        neutral = FALSE;
    } else
      nextProbe[i] = now + 1000;
  }
  static UINT timerDelay;
  UINT delay = any ? 24 : 1000;
  if (timerDelay != delay) {
    SetTimer(mainwin, 1, delay, NULL);
    timerDelay = delay;
  }
  if (learning) {
    if (!any || (owner >= 0 && !connected[owner])) {
      owner = -1;
      candidate = 0;
      armed = FALSE;
      show(status, L"Connect a controller, then release all controls.");
      return;
    }
    if (!armed) {
      if (neutral) {
        armed = TRUE;
        show(status, L"Hold the new shortcut together, then release it.");
      }
      return;
    }
    if (owner < 0)
      for (int i = 0; i < 4; i++)
        if (m[i]) {
          owner = i;
          break;
        }
    if (owner < 0)
      return;
    if (m[owner]) {
      if ((m[owner] & candidate) == candidate)
        candidate = m[owner];
      describe(status, L"Release to save     ", candidate);
      return;
    }
    if (RegSetKeyValueW(HKEY_CURRENT_USER, config, L"Shortcut", REG_DWORD,
                        &candidate, sizeof(candidate)) == ERROR_SUCCESS)
      shortcut = candidate;
    else
      MessageBoxW(mainwin, L"Windows could not save this shortcut.", L"PadClip",
                  MB_ICONWARNING);
    finish();
    return;
  }
  BOOL fire = FALSE;
  for (int i = 0; i < 4; i++) {
    if (!(m[i] & shortcut))
      latched[i] = FALSE;
    if ((m[i] & shortcut) == shortcut && !latched[i]) {
      latched[i] = TRUE;
      activePad = i;
      fire = TRUE;
    }
  }
  if (fire)
    toggle();
  show(status,
       any ? L"Controller connected. Minimize PadClip and open your game."
           : L"Waiting for an Xbox-compatible controller.");
}
static HWND child(const wchar_t *type, const wchar_t *text, int x, int y, int w,
                  int h, int id) {
  HWND c = CreateWindowW(type, text, WS_CHILD | WS_VISIBLE, x, y, w, h, mainwin,
                         (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
  SendMessageW(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
  return c;
}
static void exitPadClip(void) {
  if (worker) {
    closing = TRUE;
    stopping = TRUE;
    SetEvent(stopEvent);
    show(details, L"Saving before exit...");
  } else {
    DestroyWindow(mainwin);
  }
}
static LRESULT CALLBACK handler(HWND h, UINT msg, WPARAM w, LPARAM l) {
  if (explorerRestart && msg == explorerRestart && inTray) {
    if (!trayIcon(NIM_ADD))
      restore();
    return 0;
  }
  switch (msg) {
  case WM_SYSCOMMAND:
    if ((w & 0xfff0) == SC_MINIMIZE && trayMode) {
      minimize(trayMode);
      return 0;
    }
    break;
  case WM_APP + 4:
    restore();
    return 0;
  case WM_APP + 3:
    if (l == WM_LBUTTONUP)
      restore();
    if (l == WM_RBUTTONUP) {
      POINT point;
      GetCursorPos(&point);
      HMENU menu = CreatePopupMenu();
      AppendMenuW(menu, MF_STRING, TRAY_OPEN, L"Open PadClip");
      AppendMenuW(menu, MF_STRING, TRAY_EXIT, L"Exit");
      SetForegroundWindow(h);
      TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, h, NULL);
      DestroyMenu(menu);
    }
    return 0;
  case WM_COMMAND:
    switch (LOWORD(w)) {
    case 1:
      if (learning)
        finish();
      else {
        learning = TRUE;
        EnableWindow(updateButton, FALSE);
        armed = FALSE;
        candidate = 0;
        owner = -1;
        show(change, L"Cancel");
        show(status, L"Release all controls.");
      }
      break;
    case 2:
      minimize(trayMode);
      break;
    case TRAY_OPEN:
      restore();
      break;
    case TRAY_EXIT:
      exitPadClip();
      break;
    case 4:
      saveTrayPreference(trayBox, L"Tray", &trayMode);
      break;
    case 7:
      saveTrayPreference(closeTrayBox, L"CloseToTray", &closeToTray);
      break;
    case 5:
      if (!setStartup(SendMessageW(startupBox, BM_GETCHECK, 0, 0) ==
                      BST_CHECKED)) {
        SendMessageW(startupBox, BM_SETCHECK, startupEnabled(), 0);
        MessageBoxW(h, L"Could not update the startup setting.", L"PadClip",
                    MB_ICONWARNING);
      }
      break;
    case 6:
      if (!worker && !updating && !closing && !learning && beginUpdate(h)) {
        updating = TRUE;
        EnableWindow(updateButton, FALSE);
        EnableWindow(recordButton, FALSE);
        EnableWindow(change, FALSE);
        show(updateButton, L"Checking for updates...");
        SetTimer(h, 4, 150, NULL);
      }
      break;
    case 3:
      if (!learning)
        toggle();
      break;
    }
    return 0;
  case WM_APP + 1:
    if (!stopping)
      notify(w ? L"Recording started" : L"Recording started (video only)");
    return 0;
  case WM_APP + 2: {
    HRESULT hr = (HRESULT)(DWORD)w;
    CloseHandle(worker);
    CloseHandle(stopEvent);
    worker = NULL;
    EnableWindow(updateButton, TRUE);
    stopEvent = NULL;
    stopping = FALSE;
    EnableWindow(change, TRUE);
    show(recordButton, L"Start recording");
    if (FAILED(hr)) {
      wchar_t error[260];
      StringCchPrintfW(error, 260, L"Recording failed while %ls (0x%08lX).",
                       stage, (unsigned long)hr);
      notify(error);
    } else {
      notify(L"Clip saved in Videos \\ PadClip");
      show(details, path);
    }
    if (closing)
      DestroyWindow(h);
    return 0;
  }
  case WM_CLOSE:
    if (closeToTray && !closing)
      minimize(TRUE);
    else
      exitPadClip();
    return 0;
  case WM_TIMER:
    if (w == 1)
      poll();
    else if (w == 2) {
      KillTimer(h, 2);
      ShowWindow(notice, SW_HIDE);
    } else if (w == 4 && updating) {
      DWORD code;
      if (updateReady()) {
        KillTimer(h, 4);
        DestroyWindow(h);
      } else if (updateFinished(&code)) {
        KillTimer(h, 4);
        closeUpdateHandles();
        updating = FALSE;
        show(updateButton, L"Check for updates?");
        EnableWindow(updateButton, TRUE);
        EnableWindow(recordButton, TRUE);
        EnableWindow(change, TRUE);
        if (code)
          MessageBoxW(h, L"The update checker could not run. Please try again.",
                      L"PadClip updates", MB_ICONWARNING);
      }
    } else if (w == 3) {
      KillTimer(h, 3);
      XINPUT_VIBRATION v = {0};
      if (activePad >= 0)
        XInputSetState(activePad, &v);
    }
    return 0;
  case WM_DESTROY:
    if (inTray)
      trayIcon(NIM_DELETE);
    closeUpdateHandles();
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(h, msg, w, l);
}
int WINAPI wWinMain(HINSTANCE app, HINSTANCE prev, PWSTR cmd, int mode) {
  (void)prev;
  (void)cmd;
  SetProcessDPIAware();
  HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\8BitTradePadClip");
  if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND h = FindWindowW(L"8BitTradePadClip", NULL);
    if (h) {
      PostMessageW(h, WM_APP + 4, 0, 0);
    }
    return 0;
  }
  DWORD v = 0, size = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER, config, L"Shortcut", RRF_RT_REG_DWORD,
                   NULL, &v, &size) == ERROR_SUCCESS &&
      v && !(v & ~0x03fff3ffu))
    shortcut = v;
  trayMode = readTrayPreference(L"Tray");
  closeToTray = readTrayPreference(L"CloseToTray");
  explorerRestart = RegisterWindowMessageW(L"TaskbarCreated");
  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.lpszClassName = L"8BitTradePadClip";
  wc.hInstance = app;
  wc.lpfnWndProc = handler;
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  LPCWSTR iconId = MAKEINTRESOURCEW(1);
  wc.hIcon = LoadImageW(app, iconId, IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                        GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
  smallAppIcon = LoadImageW(app, iconId, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                            GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
  wc.hIconSm = smallAppIcon;
  if (!RegisterClassExW(&wc))
    return 1;
  mainwin =
      CreateWindowW(wc.lpszClassName, L"PadClip  |  8BitTrade",
                    WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                    CW_USEDEFAULT, 560, 461, NULL, NULL, app, NULL);
  if (!mainwin)
    return 1;
  binding = child(L"STATIC", L"", 18, 22, 510, 38, 0);
  status = child(L"STATIC", L"", 18, 68, 510, 38, 0);
  change = child(L"BUTTON", L"", 18, 116, 160, 35, 1);
  child(L"BUTTON", L"Minimize", 190, 116, 120, 35, 2);
  recordButton = child(L"BUTTON", L"Start recording", 324, 116, 160, 35, 3);
  details = child(L"STATIC", L"Ready to record.", 18, 174, 510, 58, 0);
  child(L"STATIC", L"Settings", 18, 235, 510, 22, 0);
  trayBox = child(L"BUTTON", L"Minimize to system tray", 18, 261, 460, 27, 4);
  closeTrayBox = child(L"BUTTON", L"Close to system tray", 18, 292, 460, 27, 7);
  startupBox = child(L"BUTTON", L"Start minimized when I sign in to Windows",
                     18, 323, 480, 28, 5);
  updateButton = child(L"BUTTON", L"Check for updates?", 18, 363, 178, 32, 6);
  SendMessageW(trayBox, BM_SETSTYLE, BS_AUTOCHECKBOX, TRUE);
  SendMessageW(closeTrayBox, BM_SETSTYLE, BS_AUTOCHECKBOX, TRUE);
  SendMessageW(closeTrayBox, BM_SETCHECK, closeToTray, 0);
  SendMessageW(startupBox, BM_SETSTYLE, BS_AUTOCHECKBOX, TRUE);
  SendMessageW(trayBox, BM_SETCHECK, trayMode, 0);
  SendMessageW(startupBox, BM_SETCHECK, startupEnabled(), 0);
  notice = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
      L"STATIC", L"", WS_POPUP | WS_BORDER | SS_CENTER | SS_CENTERIMAGE, 0, 0,
      360, 78, NULL, NULL, app, NULL);
  SetWindowDisplayAffinity(notice, 0x11);
  finish();
  poll();
  if (lstrcmpW(cmd, L"--startup") == 0)
    minimize(trayMode);
  else {
    ShowWindow(mainwin, mode);
    showReleaseNotes(mainwin, lstrcmpW(cmd, L"--updated") == 0);
  }
  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  UnregisterClassW(wc.lpszClassName, app);
  DestroyIcon(smallAppIcon);
  DestroyIcon(wc.hIcon);
  CloseHandle(mutex);
  return 0;
}
