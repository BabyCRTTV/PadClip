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
static BOOL waitingForTray;
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
// A sign-in window starts hidden. Explorer may accept icons only later.
static void registerHiddenTrayIcon(void) {
  inTray = trayIcon(NIM_ADD) || trayIcon(NIM_MODIFY);
  waitingForTray = !inTray;
  if (waitingForTray)
    SetTimer(mainwin, 5, 2000, NULL);
  else
    KillTimer(mainwin, 5);
}
static void restore(void) {
  waitingForTray = FALSE;
  KillTimer(mainwin, 5);
  if (inTray) {
    trayIcon(NIM_DELETE);
    inTray = FALSE;
  }
  SetWindowPos(mainwin, NULL, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
  if (IsIconic(mainwin))
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
static const wchar_t approvalKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

static BOOL startupBlocked(void) {
  BYTE data[32];
  DWORD size = sizeof(data);
  return RegGetValueW(HKEY_CURRENT_USER, approvalKey, L"PadClip", RRF_RT_REG_BINARY,
                      NULL, data, &size) == ERROR_SUCCESS && size &&
         (data[0] == 3 || data[0] == 7);
}

static LSTATUS startupCommand(wchar_t command[MAX_PATH]) {
  wchar_t exe[MAX_PATH];
  DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH);
  if (!len)
    return GetLastError();
  if (len >= MAX_PATH ||
      FAILED(StringCchPrintfW(command, MAX_PATH, L"\"%ls\" --startup", exe)))
    return ERROR_FILENAME_EXCED_RANGE;
  return ERROR_SUCCESS;
}

static LSTATUS setStartup(BOOL enable) {
  if (!enable) {
    LSTATUS err = RegDeleteKeyValueW(HKEY_CURRENT_USER, runKey, L"PadClip");
    return err == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : err;
  }
  wchar_t command[MAX_PATH];
  LSTATUS err = startupCommand(command);
  if (err != ERROR_SUCCESS)
    return err;
  return RegSetKeyValueW(HKEY_CURRENT_USER, runKey, L"PadClip", REG_SZ, command,
                         (lstrlenW(command) + 1) * sizeof(*command));
}

static BOOL startupArgument(const wchar_t *cmd) {
  int count = 0;
  wchar_t **args = CommandLineToArgvW(cmd, &count);
  BOOL found = FALSE;
  for (int i = 0; args && i < count; ++i)
    if (!lstrcmpiW(args[i], L"--startup"))
      found = TRUE;
  LocalFree(args);
  return found;
}

static BOOL hasStartupEntry(void) {
  return RegGetValueW(HKEY_CURRENT_USER, runKey, L"PadClip", RRF_RT_REG_SZ,
                      NULL, NULL, NULL) == ERROR_SUCCESS;
}

static void initializeStartupDefault(void) {
  HKEY key;
  DWORD disposition;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, config, 0, NULL, 0, KEY_QUERY_VALUE,
                      NULL, &key, &disposition) != ERROR_SUCCESS)
    return;
  RegCloseKey(key);
  if (disposition == REG_CREATED_NEW_KEY && !startupBlocked() && !hasStartupEntry())
    setStartup(TRUE);
}

// An existing registration is the user's opt-in, including in older versions.
// Do not recreate a registration the user removed or change Windows' approval.
static void refreshStartup(BOOL repair) {
  if (!startupBox)
    return;
  wchar_t actual[MAX_PATH], wanted[MAX_PATH];
  DWORD size = sizeof(actual);
  LSTATUS err = RegGetValueW(HKEY_CURRENT_USER, runKey, L"PadClip", RRF_RT_REG_SZ,
                             NULL, actual, &size);
  BOOL registered = err == ERROR_SUCCESS || err == ERROR_MORE_DATA;
  BOOL valid = err == ERROR_SUCCESS && startupCommand(wanted) == ERROR_SUCCESS &&
               lstrcmpiW(actual, wanted) == 0;
  BOOL blocked = startupBlocked();
  if (registered && !valid && repair && !blocked)
    valid = setStartup(TRUE) == ERROR_SUCCESS;
  SetWindowTextW(startupBox, blocked && registered
      ? L"Start in the tray at sign-in (disabled in Windows)"
      : registered && !valid ? L"Start in the tray at sign-in (path needs repair)"
                             : L"Start in the system tray when I sign in");
  SendMessageW(startupBox, BM_SETCHECK, valid && !blocked, 0);
}

static void changeStartup(void) {
  BOOL enable = SendMessageW(startupBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
  LSTATUS err = setStartup(enable);
  refreshStartup(FALSE);
  if (err != ERROR_SUCCESS) {
    wchar_t message[240];
    if (err == ERROR_FILENAME_EXCED_RANGE)
      StringCchCopyW(message, ARRAYSIZE(message),
                     L"The sign-in command is too long for Windows. Move PadClip to a shorter folder path and try again.");
    else
      StringCchPrintfW(message, ARRAYSIZE(message),
                       L"Windows could not save the sign-in setting (error %ld).", err);
    MessageBoxW(mainwin, message, L"PadClip", MB_ICONWARNING);
  } else if (enable && startupBlocked() &&
             MessageBoxW(mainwin, L"Windows has disabled PadClip under Startup apps. Open that setting to enable it?",
                         L"PadClip", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
    if ((INT_PTR)ShellExecuteW(mainwin, L"open", L"ms-settings:startupapps",
                                NULL, NULL, SW_SHOWNORMAL) <= 32)
      MessageBoxW(mainwin, L"Open Task Manager, choose Startup apps, and enable PadClip.",
                  L"PadClip", MB_ICONINFORMATION);
  }
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
static HWND minimizeButton, settingsLabel;
static HFONT uiFont;
static UINT uiDpi = 96;
static BOOL controlsReady;
static const DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;

static int px(int logical) { return MulDiv(logical, uiDpi, 96); }

static HWND child(const wchar_t *type, const wchar_t *text, int id, DWORD style) {
  return CreateWindowW(type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
                       mainwin, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
}

static void updateFont(void) {
  HFONT font = CreateFontW(-MulDiv(10, uiDpi, 72), 0, 0, 0, FW_NORMAL, FALSE,
                          FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH, L"Segoe UI");
  if (!font)
    return;
  for (HWND c = GetWindow(mainwin, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
  if (uiFont)
    DeleteObject(uiFont);
  uiFont = font;
}

static RECT workArea(HWND window) {
  MONITORINFO monitor = {0};
  monitor.cbSize = sizeof(monitor);
  GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
  return monitor.rcWork;
}

static UINT windowDpi(HWND window) {
  typedef UINT (WINAPI *WindowDpiFn)(HWND);
  WindowDpiFn getDpi = (WindowDpiFn)GetProcAddress(GetModuleHandleW(L"user32"), "GetDpiForWindow");
  if (getDpi)
    return getDpi(window);
  HDC dc = GetDC(window);
  UINT dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
  if (dc)
    ReleaseDC(window, dc);
  return dpi;
}

// Use client dimensions for layout, then add the title bar and resize borders.
static SIZE outerSize(int width, int height) {
  RECT r = {0, 0, px(width), px(height)};
  typedef BOOL (WINAPI *AdjustForDpiFn)(LPRECT, DWORD, BOOL, DWORD, UINT);
  AdjustForDpiFn adjust = (AdjustForDpiFn)GetProcAddress(GetModuleHandleW(L"user32"), "AdjustWindowRectExForDpi");
  if (!adjust || !adjust(&r, windowStyle, FALSE, 0, uiDpi))
    AdjustWindowRectEx(&r, windowStyle, FALSE, 0);
  SIZE size = {r.right - r.left, r.bottom - r.top};
  return size;
}

static void fitToDisplay(HWND window, RECT requested) {
  MONITORINFO monitor = {0};
  monitor.cbSize = sizeof(monitor);
  GetMonitorInfoW(MonitorFromRect(&requested, MONITOR_DEFAULTTONEAREST), &monitor);
  RECT area = monitor.rcWork;
  int width = min(requested.right - requested.left, area.right - area.left);
  int height = min(requested.bottom - requested.top, area.bottom - area.top);
  int x = max(area.left, min(requested.left, area.right - width));
  int y = max(area.top, min(requested.top, area.bottom - height));
  SetWindowPos(window, NULL, x, y, width, height, SWP_NOACTIVATE | SWP_NOZORDER);
}

static void place(HWND control, int x, int y, int w, int h, int scroll) {
  // Convert edges separately so adjacent controls still align at fractional DPI.
  MoveWindow(control, px(x), px(y) - scroll, px(x+w) - px(x), px(y+h) - px(y), TRUE);
}

static void layout(void) {
  if (!controlsReady || IsIconic(mainwin))
    return;
  RECT client;
  GetClientRect(mainwin, &client);
  int width = MulDiv(client.right, 96, uiDpi);
  const int margin = 14, gap = 8;
  int content = max(1, width - 2 * margin);
  int columns = content >= 430 ? 3 : content >= 280 ? 2 : 1;
  int rows = (3 + columns - 1) / columns;
  int actionsY = 98, detailsY = actionsY + rows * 42 + 4;
  int checkH = content < 330 ? 40 : 30;
  int settingsY = detailsY + 58;
  int checksY = settingsY + 26;
  int updateY = checksY + checkH * 3 + 6;
  SCROLLINFO si = {0};
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE;
  si.nMax = px(updateY + 36 + margin) - 1;
  si.nPage = client.bottom;
  SetScrollInfo(mainwin, SB_VERT, &si, TRUE);
  RECT actual;
  GetClientRect(mainwin, &actual);
  if (actual.right != client.right) {
    layout();
    return;
  }
  int scroll = GetScrollPos(mainwin, SB_VERT);
  place(binding, margin, 12, content, 40, scroll);
  place(status, margin, 55, content, 40, scroll);
  HWND actions[] = {change, minimizeButton, recordButton};
  for (int i = 0; i < 3; ++i) {
    int col = i % columns;
    int left = col * (content + gap) / columns;
    int right = (col+1) * (content + gap) / columns - gap;
    place(actions[i], margin + left, actionsY + i / columns * 42, right - left, 34, scroll);
  }
  place(details, margin, detailsY, content, 54, scroll);
  place(settingsLabel, margin, settingsY, content, 24, scroll);
  place(trayBox, margin, checksY, content, checkH, scroll);
  place(closeTrayBox, margin, checksY + checkH, content, checkH, scroll);
  place(startupBox, margin, checksY + checkH * 2, content, checkH, scroll);
  place(updateButton, margin, updateY, min(content, 190), 36, scroll);
}

static void scrollTo(int pos) {
  SetScrollPos(mainwin, SB_VERT, pos, TRUE);
  layout();
}

static void scrollCommand(WPARAM command) {
  SCROLLINFO si = {0};
  si.cbSize = sizeof(si);
  si.fMask = SIF_ALL;
  GetScrollInfo(mainwin, SB_VERT, &si);
  int pos = si.nPos;
  switch (LOWORD(command)) {
  case SB_LINEUP: pos -= px(24); break;
  case SB_LINEDOWN: pos += px(24); break;
  case SB_PAGEUP: pos -= (int)si.nPage; break;
  case SB_PAGEDOWN: pos += (int)si.nPage; break;
  case SB_TOP: pos = si.nMin; break;
  case SB_BOTTOM: pos = si.nMax; break;
  case SB_THUMBPOSITION: case SB_THUMBTRACK: pos = si.nTrackPos; break;
  default: return;
  }
  scrollTo(pos);
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
  if (explorerRestart && msg == explorerRestart && (inTray || waitingForTray)) {
    registerHiddenTrayIcon();
    return 0;
  }
  switch (msg) {
  case WM_SIZE:
    layout();
    return 0;
  case WM_GETMINMAXINFO: {
    RECT area = workArea(h);
    SIZE minimum = outerSize(280, 220);
    MINMAXINFO *limits = (MINMAXINFO *)l;
    limits->ptMinTrackSize.x = min(minimum.cx, area.right - area.left);
    limits->ptMinTrackSize.y = min(minimum.cy, area.bottom - area.top);
    return 0;
  }
  case WM_DPICHANGED:
    uiDpi = HIWORD(w);
    if (controlsReady) {
      updateFont();
      fitToDisplay(h, *(RECT *)l);
      layout();
    }
    return 0;
  case WM_EXITSIZEMOVE:
  case WM_DISPLAYCHANGE:
  case WM_SETTINGCHANGE:
    if (controlsReady && (msg != WM_SETTINGCHANGE || w == SPI_SETWORKAREA) &&
        !IsIconic(h) && !IsZoomed(h)) {
      RECT current;
      GetWindowRect(h, &current);
      fitToDisplay(h, current);
    }
    break;
  case WM_VSCROLL:
    scrollCommand(w);
    return 0;
  case WM_MOUSEWHEEL:
    scrollTo(GetScrollPos(h, SB_VERT) - MulDiv(GET_WHEEL_DELTA_WPARAM(w), px(48), WHEEL_DELTA));
    return 0;
  case WM_ACTIVATE:
    if (LOWORD(w) != WA_INACTIVE && controlsReady)
      refreshStartup(FALSE);
    break;
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
      changeStartup();
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
    } else if (w == 5 && waitingForTray) {
      registerHiddenTrayIcon();
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

  BOOL signIn = startupArgument(cmd);
  if (signIn && (!hasStartupEntry() || startupBlocked()))
    return 0;
  HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\8BitTradePadClip");
  if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND h = FindWindowW(L"8BitTradePadClip", NULL);
    if (h && !signIn) {
      PostMessageW(h, WM_APP + 4, 0, 0);
    }
    return 0;
  }
  if (!signIn)
    initializeStartupDefault();
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
                    windowStyle, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                    CW_USEDEFAULT, NULL, NULL, app, NULL);
  if (!mainwin)
    return 1;
  uiDpi = windowDpi(mainwin);
  binding = child(L"STATIC", L"", 0, SS_NOPREFIX);
  status = child(L"STATIC", L"", 0, SS_NOPREFIX);
  change = child(L"BUTTON", L"", 1, BS_PUSHBUTTON);
  minimizeButton = child(L"BUTTON", L"Minimize", 2, BS_PUSHBUTTON);
  recordButton = child(L"BUTTON", L"Start recording", 3, BS_PUSHBUTTON);
  details = child(L"STATIC", L"Ready to record.", 0, SS_NOPREFIX);
  settingsLabel = child(L"STATIC", L"Settings", 0, SS_NOPREFIX);
  DWORD checkStyle = BS_AUTOCHECKBOX | BS_MULTILINE;
  trayBox = child(L"BUTTON", L"Minimize to system tray", 4, checkStyle);
  closeTrayBox = child(L"BUTTON", L"Close to system tray", 7, checkStyle);
  startupBox = child(L"BUTTON", L"", 5, checkStyle);
  updateButton = child(L"BUTTON", L"Check for updates?", 6, BS_PUSHBUTTON);
  controlsReady = TRUE;
  updateFont();
  RECT area = workArea(mainwin);
  SIZE initial = outerSize(552, 396);
  int x = area.left + max(0, (area.right - area.left - initial.cx) / 2);
  int y = area.top + max(0, (area.bottom - area.top - initial.cy) / 2);
  RECT initialRect = {x, y, x + initial.cx, y + initial.cy};
  fitToDisplay(mainwin, initialRect);
  layout();
  SendMessageW(closeTrayBox, BM_SETCHECK, closeToTray, 0);
  SendMessageW(trayBox, BM_SETCHECK, trayMode, 0);
  refreshStartup(TRUE);
  notice = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
      L"STATIC", L"", WS_POPUP | WS_BORDER | SS_CENTER | SS_CENTERIMAGE, 0, 0,
      360, 78, NULL, NULL, app, NULL);
  SetWindowDisplayAffinity(notice, 0x11);
  finish();
  poll();
  if (signIn) {
    registerHiddenTrayIcon();
  } else {
    ShowWindow(mainwin, mode);
    showReleaseNotes(mainwin, lstrcmpW(cmd, L"--updated") == 0);
  }
  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (uiFont)
    DeleteObject(uiFont);
  UnregisterClassW(wc.lpszClassName, app);
  DestroyIcon(smallAppIcon);
  DestroyIcon(wc.hIcon);
  CloseHandle(mutex);
  return 0;
}
