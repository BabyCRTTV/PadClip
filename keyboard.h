#ifndef PADCLIP_KEYBOARD_H
#define PADCLIP_KEYBOARD_H
#include <windows.h>

#define KEYBOARD_DIALOG 200
#define KEY_PAGE 201
#define KEY_PREVIEW 202
#define KEY_STEP 203
#define KEY_PREVIOUS 204
#define KEY_NEXT 205
#define KEY_REMOVE 206
#define KEY_CLEAR 207
#define KEY_OTHER 208
#define KEY_ADD 209
#define KEY_GRID 1000

#ifndef RC_INVOKED
enum { MAX_STEPS = 8, MAX_CHORD = 8, STEP_TIMEOUT = 1200, NUM_ENTER = 255 };
typedef struct {
  BYTE count, key[MAX_CHORD];
} KeyChord;
typedef struct {
  BYTE count;
  KeyChord step[MAX_STEPS];
} KeyBinding;
typedef struct {
  BYTE down[256];
  ULONGLONG until[MAX_STEPS];
} KeyState;

BOOL keyTransition(KeyState *state, const KeyBinding *binding, BYTE key,
                   BOOL down, ULONGLONG now);
void keyboardLoad(HWND owner, const wchar_t *config);
void keyboardEdit(HWND owner, const wchar_t *config);
void keyboardReset(void);
void keyboardDismiss(void);
BOOL keyboardInput(HRAWINPUT input, BOOL enabled);
#endif
#endif
