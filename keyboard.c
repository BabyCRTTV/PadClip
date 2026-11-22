#include "keyboard.h"
#include <stddef.h>
#include <strsafe.h>

static KeyBinding saved, draft;
static KeyState state;
static HWND editor;
static int stepIndex;
static const wchar_t *configPath;

static BOOL includes(const KeyChord *chord, BYTE key) {
  for (int i = 0; i < chord->count; ++i)
    if (chord->key[i] == key)
      return TRUE;
  return FALSE;
}

static BYTE family(BYTE key) {
  if (key == VK_LCONTROL || key == VK_RCONTROL)
    return VK_CONTROL;
  if (key == VK_LSHIFT || key == VK_RSHIFT)
    return VK_SHIFT;
  if (key == VK_LMENU || key == VK_RMENU)
    return VK_MENU;
  return key;
}

static BOOL held(const KeyState *s, BYTE key) {
  switch (key) {
  case VK_CONTROL:
    return s->down[VK_LCONTROL] || s->down[VK_RCONTROL];
  case VK_SHIFT:
    return s->down[VK_LSHIFT] || s->down[VK_RSHIFT];
  case VK_MENU:
    return s->down[VK_LMENU] || s->down[VK_RMENU];
  default:
    return s->down[key];
  }
}

static BOOL complete(const KeyState *s, const KeyChord *chord) {
  for (int i = 0; i < chord->count; ++i)
    if (!held(s, chord->key[i]))
      return FALSE;
  return chord->count != 0;
}

BOOL keyTransition(KeyState *s, const KeyBinding *binding, BYTE key, BOOL down,
                   ULONGLONG now) {
  if (!binding->count || s->down[key] == down)
    return FALSE;
  if (!down) {
    s->down[key] = FALSE;
    return FALSE;
  }
  BOOL wasHeld = held(s, family(key));
  s->down[key] = TRUE;
  BOOL fire = FALSE;
  // Backwards so a single down event cannot advance two steps. Each prefix
  // can start independently, including overlaps such as A, A, B.
  for (int i = binding->count - 1; i >= 0; --i) {
    if (i && (!s->until[i] || now > s->until[i]))
      continue;
    const KeyChord *chord = &binding->step[i];
    BOOL exact = includes(chord, key);
    if (!exact && !includes(chord, family(key))) {
      s->until[i] = 0;
      continue;
    }
    if ((!exact && wasHeld) || !complete(s, chord))
      continue;
    s->until[i] = 0;
    if (i + 1 == binding->count)
      fire = TRUE;
    else
      s->until[i + 1] = now + STEP_TIMEOUT;
  }
  if (fire)
    ZeroMemory(s->until, sizeof(s->until));
  return fire;
}

static BOOL valid(const KeyBinding *b) {
  if (b->count > MAX_STEPS)
    return FALSE;
  for (int i = 0; i < b->count; ++i) {
    const KeyChord *c = &b->step[i];
    if (!c->count || c->count > MAX_CHORD)
      return FALSE;
    for (int j = 0; j < c->count; ++j) {
      if (c->key[j] < VK_BACK)
        return FALSE;
      for (int k = 0; k < j; ++k)
        if (c->key[j] == c->key[k])
          return FALSE;
    }
  }
  return TRUE;
}

static BOOL registerKeyboard(HWND owner, BOOL on) {
  RAWINPUTDEVICE device = {
      1, 6, on ? RIDEV_INPUTSINK | RIDEV_DEVNOTIFY : RIDEV_REMOVE,
      on ? owner : NULL};
  return RegisterRawInputDevices(&device, 1, sizeof(device));
}

void keyboardReset(void) { ZeroMemory(&state, sizeof(state)); }
void keyboardDismiss(void) {
  if (editor)
    EndDialog(editor, IDCANCEL);
}

void keyboardLoad(HWND owner, const wchar_t *config) {
  DWORD size = sizeof(saved);
  if (RegGetValueW(HKEY_CURRENT_USER, config, L"KeyboardShortcutV1",
                   RRF_RT_REG_BINARY, NULL, &saved, &size) != ERROR_SUCCESS ||
      size != sizeof(saved) || !valid(&saved))
    ZeroMemory(&saved, sizeof(saved));
  if (saved.count && !registerKeyboard(owner, TRUE))
    SetWindowTextW(GetDlgItem(owner, 8), L"Retry keyboard shortcut");
}

BOOL keyboardInput(HRAWINPUT handle, BOOL enabled) {
  if (!enabled || !saved.count || editor) {
    keyboardReset();
    return FALSE;
  }
  RAWINPUT input;
  UINT size = sizeof(input);
  UINT bytes =
      GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));
  if (bytes == (UINT)-1 ||
      bytes < offsetof(RAWINPUT, data) + sizeof(RAWKEYBOARD) ||
      input.header.dwType != RIM_TYPEKEYBOARD)
    return FALSE;
  RAWKEYBOARD k = input.data.keyboard;
  if (k.VKey < VK_BACK || k.VKey >= 255 ||
      k.MakeCode == KEYBOARD_OVERRUN_MAKE_CODE)
    return FALSE;
  BYTE key = (BYTE)k.VKey;
  BOOL extended = (k.Flags & RI_KEY_E0) != 0;
  if (key == VK_SHIFT)
    key = k.MakeCode == 0x36 ? VK_RSHIFT : VK_LSHIFT;
  else if (key == VK_CONTROL)
    key = extended ? VK_RCONTROL : VK_LCONTROL;
  else if (key == VK_MENU)
    key = extended ? VK_RMENU : VK_LMENU;
  else if (key == VK_RETURN && extended)
    key = NUM_ENTER;
  // Some keyboards emit only a break for Print Screen or only a make for Pause.
  ULONGLONG now = GetTickCount64();
  BOOL down = !(k.Flags & RI_KEY_BREAK);
  if (key == VK_SNAPSHOT && !down && !state.down[key]) {
    BOOL fire = keyTransition(&state, &saved, key, TRUE, now);
    keyTransition(&state, &saved, key, FALSE, now);
    return fire;
  }
  BOOL fire = keyTransition(&state, &saved, key, down, now);
  if (key == VK_PAUSE && down)
    keyTransition(&state, &saved, key, FALSE, now);
  return fire;
}

// Rows are laid out as a keyboard. The separate key list includes less common
// layout-specific keys and separate left/right modifiers.
static const BYTE mainRows[][16] = {
    {VK_ESCAPE, VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9,
     VK_F10, VK_F11, VK_F12},
    {VK_OEM_3, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', VK_OEM_MINUS,
     VK_OEM_PLUS, VK_BACK},
    {VK_TAB, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', VK_OEM_4,
     VK_OEM_6, VK_OEM_5},
    {VK_CAPITAL, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', VK_OEM_1,
     VK_OEM_7, VK_RETURN},
    {VK_SHIFT, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', VK_OEM_COMMA, VK_OEM_PERIOD,
     VK_OEM_2},
    {VK_CONTROL, VK_LWIN, VK_MENU, VK_SPACE, VK_RWIN, VK_APPS}};
static const BYTE numRows[][16] = {
    {VK_SNAPSHOT, VK_SCROLL, VK_PAUSE, VK_NUMLOCK, VK_DIVIDE, VK_MULTIPLY,
     VK_SUBTRACT},
    {VK_INSERT, VK_HOME, VK_PRIOR, VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9, VK_ADD},
    {VK_DELETE, VK_END, VK_NEXT, VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6},
    {VK_UP, VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, NUM_ENTER},
    {VK_LEFT, VK_DOWN, VK_RIGHT, VK_NUMPAD0, VK_DECIMAL}};
static const BYTE extraRows[][16] = {
    {VK_F13, VK_F14, VK_F15, VK_F16, VK_F17, VK_F18, VK_F19, VK_F20, VK_F21,
     VK_F22, VK_F23, VK_F24},
    {VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU},
    {VK_VOLUME_MUTE, VK_VOLUME_DOWN, VK_VOLUME_UP, VK_MEDIA_PREV_TRACK,
     VK_MEDIA_NEXT_TRACK, VK_MEDIA_PLAY_PAUSE, VK_MEDIA_STOP},
    {VK_BROWSER_BACK, VK_BROWSER_FORWARD, VK_BROWSER_REFRESH, VK_BROWSER_HOME}};

static void keyName(BYTE key, wchar_t name[64]) {
  static const struct {
    BYTE key;
    const wchar_t *label;
  } names[] = {{VK_ESCAPE, L"Esc"},
               {VK_BACK, L"Bksp"},
               {VK_CAPITAL, L"Caps"},
               {VK_RETURN, L"Enter"},
               {VK_CONTROL, L"Ctrl"},
               {VK_MENU, L"Alt"},
               {VK_SHIFT, L"Shift"},
               {VK_LWIN, L"L Win"},
               {VK_RWIN, L"R Win"},
               {VK_APPS, L"Menu"},
               {VK_LSHIFT, L"L Shift"},
               {VK_RSHIFT, L"R Shift"},
               {VK_LCONTROL, L"L Ctrl"},
               {VK_RCONTROL, L"R Ctrl"},
               {VK_LMENU, L"L Alt"},
               {VK_RMENU, L"R Alt"},
               {VK_NUMLOCK, L"Num Lock"},
               {NUM_ENTER, L"Num Enter"},
               {VK_PRIOR, L"PgUp"},
               {VK_NEXT, L"PgDn"},
               {VK_SNAPSHOT, L"PrtSc"},
               {VK_VOLUME_MUTE, L"Mute"},
               {VK_VOLUME_UP, L"Vol +"},
               {VK_VOLUME_DOWN, L"Vol -"},
               {VK_MEDIA_PREV_TRACK, L"Prev track"},
               {VK_MEDIA_NEXT_TRACK, L"Next track"},
               {VK_MEDIA_STOP, L"Stop"},
               {VK_MEDIA_PLAY_PAUSE, L"Play/pause"},
               {VK_BROWSER_BACK, L"Back"},
               {VK_BROWSER_FORWARD, L"Forward"},
               {VK_BROWSER_REFRESH, L"Refresh"},
               {VK_BROWSER_HOME, L"Browser home"}};
  for (unsigned i = 0; i < ARRAYSIZE(names); ++i)
    if (names[i].key == key) {
      StringCchCopyW(name, 64, names[i].label);
      return;
    }
  if (key >= VK_F1 && key <= VK_F24) {
    StringCchPrintfW(name, 64, L"F%d", key - VK_F1 + 1);
    return;
  }
  if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) {
    StringCchPrintfW(name, 64, L"Num %d", key - VK_NUMPAD0);
    return;
  }
  UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
  if (!scan || !GetKeyNameTextW(((scan & 255) << 16) |
                                    ((scan & 0xff00) == 0xe000 ? 1 << 24 : 0),
                                name, 64))
    StringCchPrintfW(name, 64, L"VK %02X", key);
}

static void preview(HWND dialog) {
  wchar_t text[4800] = L"", label[32];
  for (int i = 0; i < draft.count; ++i) {
    if (i)
      StringCchCatW(text, ARRAYSIZE(text), L"  then  ");
    for (int j = 0; j < draft.step[i].count; ++j) {
      wchar_t name[64];
      keyName(draft.step[i].key[j], name);
      if (j)
        StringCchCatW(text, ARRAYSIZE(text), L" + ");
      StringCchCatW(text, ARRAYSIZE(text), name);
    }
  }
  SetDlgItemTextW(dialog, KEY_PREVIEW,
                  text[0] ? text : L"No keyboard shortcut");
  StringCchPrintfW(label, ARRAYSIZE(label), L"Step %d of %d", stepIndex + 1,
                   max(draft.count, stepIndex + 1));
  SetDlgItemTextW(dialog, KEY_STEP, label);
  EnableWindow(GetDlgItem(dialog, KEY_PREVIOUS), stepIndex > 0);
  EnableWindow(GetDlgItem(dialog, KEY_NEXT),
               draft.step[stepIndex].count && stepIndex + 1 < MAX_STEPS);
  EnableWindow(GetDlgItem(dialog, KEY_REMOVE), draft.count || stepIndex);
  for (int k = VK_BACK; k <= NUM_ENTER; ++k)
    CheckDlgButton(dialog, KEY_GRID + k,
                   includes(&draft.step[stepIndex], (BYTE)k) ? BST_CHECKED
                                                             : BST_UNCHECKED);
}

static void grid(HWND dialog) {
  for (int k = VK_BACK; k <= NUM_ENTER; ++k) {
    HWND old = GetDlgItem(dialog, KEY_GRID + k);
    if (old)
      DestroyWindow(old);
  }
  int page = (int)SendDlgItemMessageW(dialog, KEY_PAGE, CB_GETCURSEL, 0, 0);
  const BYTE(*rows)[16] = page == 0   ? mainRows
                          : page == 1 ? numRows
                                      : extraRows;
  int count = page == 0   ? ARRAYSIZE(mainRows)
              : page == 1 ? ARRAYSIZE(numRows)
                          : ARRAYSIZE(extraRows);
  for (int r = 0; r < count; ++r) {
    int length = 0;
    while (rows[r][length])
      ++length;
    for (int col = 0; col < length; ++col) {
      BYTE k = rows[r][col];
      wchar_t name[64];
      keyName(k, name);
      RECT rect = {7 + col * 346 / length, 39 + r * 16,
                   7 + (col + 1) * 346 / length - 1, 54 + r * 16};
      MapDialogRect(dialog, &rect);
      HWND button = CreateWindowW(
          L"BUTTON", name,
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHLIKE | BS_CHECKBOX,
          rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
          dialog, (HMENU)(INT_PTR)(KEY_GRID + k), GetModuleHandleW(NULL), NULL);
      SendMessageW(button, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0),
                   TRUE);
    }
  }
  preview(dialog);
}

static void selectKey(HWND dialog, BYTE key) {
  KeyChord *chord = &draft.step[stepIndex];
  int i = 0;
  while (i < chord->count && chord->key[i] != key)
    ++i;
  if (i < chord->count) {
    --chord->count;
    memmove(chord->key + i, chord->key + i + 1, chord->count - i);
    if (!chord->count && stepIndex + 1 < draft.count) {
      memmove(chord, chord + 1, (draft.count - stepIndex - 1) * sizeof(*chord));
      ZeroMemory(&draft.step[--draft.count], sizeof(*chord));
    } else if (!chord->count)
      draft.count = stepIndex;
  } else if (chord->count < MAX_CHORD) {
    chord->key[chord->count++] = key;
    draft.count = max(draft.count, stepIndex + 1);
  } else {
    MessageBoxW(dialog, L"Each step can use up to eight keys.",
                L"Keyboard shortcut", MB_OK);
  }
  preview(dialog);
}

static INT_PTR CALLBACK picker(HWND dialog, UINT msg, WPARAM w, LPARAM l) {
  (void)l;
  if (msg == WM_INITDIALOG) {
    editor = dialog;
    draft = saved;
    stepIndex = 0;
    const wchar_t *pages[] = {L"Main keyboard", L"Navigation and number pad",
                              L"Function and media keys"};
    for (unsigned i = 0; i < ARRAYSIZE(pages); ++i)
      SendDlgItemMessageW(dialog, KEY_PAGE, CB_ADDSTRING, 0, (LPARAM)pages[i]);
    SendDlgItemMessageW(dialog, KEY_PAGE, CB_SETCURSEL, 0, 0);
    for (int k = VK_BACK; k <= NUM_ENTER; ++k) {
      wchar_t name[64];
      keyName((BYTE)k, name);
      LRESULT item =
          SendDlgItemMessageW(dialog, KEY_OTHER, CB_ADDSTRING, 0, (LPARAM)name);
      if (item >= 0)
        SendDlgItemMessageW(dialog, KEY_OTHER, CB_SETITEMDATA, item, k);
    }
    SendDlgItemMessageW(dialog, KEY_OTHER, CB_SETCURSEL, 0, 0);
    grid(dialog);
    return TRUE;
  }
  if (msg == WM_CLOSE) {
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  }
  if (msg != WM_COMMAND)
    return FALSE;
  int id = LOWORD(w);
  if (id >= KEY_GRID + VK_BACK && id <= KEY_GRID + NUM_ENTER) {
    selectKey(dialog, (BYTE)(id - KEY_GRID));
    return TRUE;
  }
  switch (id) {
  case KEY_PAGE:
    if (HIWORD(w) == CBN_SELCHANGE)
      grid(dialog);
    break;
  case KEY_ADD: {
    LRESULT item = SendDlgItemMessageW(dialog, KEY_OTHER, CB_GETCURSEL, 0, 0);
    if (item >= 0) {
      BYTE key =
          (BYTE)SendDlgItemMessageW(dialog, KEY_OTHER, CB_GETITEMDATA, item, 0);
      selectKey(dialog, key);
    }
    break;
  }
  case KEY_PREVIOUS:
    if (stepIndex)
      --stepIndex;
    break;
  case KEY_NEXT:
    if (draft.step[stepIndex].count && stepIndex + 1 < MAX_STEPS)
      ++stepIndex;
    break;
  case KEY_CLEAR:
    ZeroMemory(&draft, sizeof(draft));
    stepIndex = 0;
    break;
  case KEY_REMOVE:
    if (stepIndex < draft.count) {
      memmove(&draft.step[stepIndex], &draft.step[stepIndex + 1],
              (draft.count - stepIndex - 1) * sizeof(KeyChord));
      ZeroMemory(&draft.step[--draft.count], sizeof(KeyChord));
    }
    stepIndex = min(stepIndex, max(0, draft.count - 1));
    break;
  case IDCANCEL:
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  case IDOK: {
    HWND owner = GetParent(dialog);
    if (draft.count && !registerKeyboard(owner, TRUE)) {
      MessageBoxW(dialog, L"Windows could not enable keyboard shortcuts.",
                  L"PadClip", MB_ICONWARNING);
      return TRUE;
    }
    if (RegSetKeyValueW(HKEY_CURRENT_USER, configPath, L"KeyboardShortcutV1",
                        REG_BINARY, &draft, sizeof(draft)) != ERROR_SUCCESS) {
      if (!saved.count && draft.count)
        registerKeyboard(owner, FALSE);
      MessageBoxW(dialog, L"Windows could not save the keyboard shortcut.",
                  L"PadClip", MB_ICONWARNING);
      return TRUE;
    }
    if (!draft.count && saved.count)
      registerKeyboard(owner, FALSE);
    saved = draft;
    SetWindowTextW(GetDlgItem(owner, 8), L"Keyboard shortcut");
    EndDialog(dialog, IDOK);
    return TRUE;
  }
  default:
    return FALSE;
  }
  preview(dialog);
  return TRUE;
}

void keyboardEdit(HWND owner, const wchar_t *config) {
  configPath = config;
  keyboardReset();
  DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(KEYBOARD_DIALOG),
                  owner, picker, 0);
  editor = NULL;
  keyboardReset();
}
