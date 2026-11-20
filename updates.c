#include "updates.h"
#include <strsafe.h>
#include <stdlib.h>
#include <wincrypt.h>
#include <wchar.h>

static HANDLE updater, ready;
static const wchar_t settings[] = L"Software\\8BitTrade\\PadClip";

static wchar_t *resourceText(WORD id) {
  HRSRC resource = FindResourceW(NULL, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
  if (!resource)
    return NULL;
  const char *bytes = LockResource(LoadResource(NULL, resource));
  int size = (int)SizeofResource(NULL, resource);
  int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, size, NULL, 0);
  if (!length)
    return NULL;
  wchar_t *text = calloc((size_t)length + 1, sizeof(*text));
  if (text)
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, size, text, length);
  return text;
}

static wchar_t *releaseInfo(wchar_t **notes) {
  wchar_t *version = resourceText(102);
  if (version) {
    size_t end = wcscspn(version, L"\r\n");
    *notes = version + end + wcsspn(version + end, L"\r\n");
    version[end] = 0;
  }
  return version;
}

void showReleaseNotes(HWND owner, BOOL afterUpdate) {
  wchar_t previous[64] = L"", title[100], *notes;
  wchar_t *version = releaseInfo(&notes);
  if (!version)
    return;
  DWORD size = sizeof(previous);
  LSTATUS read = RegGetValueW(HKEY_CURRENT_USER, settings, L"LastChangelog",
                              RRF_RT_REG_SZ, NULL, previous, &size);
  if (afterUpdate || read != ERROR_SUCCESS || wcscmp(version, previous)) {
    StringCchPrintfW(title, ARRAYSIZE(title), L"What's new in PadClip %ls", version);
    MessageBoxW(owner, notes, title, MB_OK | MB_ICONINFORMATION);
    RegSetKeyValueW(HKEY_CURRENT_USER, settings, L"LastChangelog", REG_SZ,
                    version, (DWORD)((wcslen(version) + 1) * sizeof(*version)));
  }
  free(version);
}

void closeUpdateHandles(void) {
  if (updater)
    CloseHandle(updater);
  if (ready)
    CloseHandle(ready);
  updater = ready = NULL;
}

BOOL updateReady(void) { return ready && WaitForSingleObject(ready, 0) == WAIT_OBJECT_0; }

BOOL updateFinished(DWORD *exitCode) {
  return updater && WaitForSingleObject(updater, 0) == WAIT_OBJECT_0 &&
         GetExitCodeProcess(updater, exitCode);
}

BOOL beginUpdate(HWND owner) {
  wchar_t shell[MAX_PATH], eventName[100], prefix[160], *notes;
  wchar_t *version = releaseInfo(&notes), *script = resourceText(101);
  wchar_t *source = NULL, *command = NULL;
  BOOL started = FALSE;
  UINT dirLength = GetSystemDirectoryW(shell, ARRAYSIZE(shell));
  if (!version || !script || !dirLength || dirLength >= ARRAYSIZE(shell) ||
      FAILED(StringCchCatW(shell, ARRAYSIZE(shell), L"\\WindowsPowerShell\\v1.0\\powershell.exe")))
    goto done;
  DWORD pid = GetCurrentProcessId();
  StringCchPrintfW(eventName, ARRAYSIZE(eventName), L"Local\\8BitTrade.PadClip.Update.%lu", pid);
  ready = CreateEventW(NULL, TRUE, FALSE, eventName);
  if (!ready || GetLastError() == ERROR_ALREADY_EXISTS)
    goto done;
  StringCchPrintfW(prefix, ARRAYSIZE(prefix), L"$ownerPid=%lu\r\n$installedVersion=[version]'%ls'\r\n", pid, version);
  size_t length = wcslen(prefix) + wcslen(script);
  source = calloc(length + 1, sizeof(*source));
  if (!source)
    goto done;
  wcscpy(source, prefix);
  wcscat(source, script);
  const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
  DWORD encoded = 0;
  DWORD bytes = (DWORD)(length * sizeof(*source));
  if (!CryptBinaryToStringW((BYTE *)source, bytes, flags, NULL, &encoded))
    goto done;
  static const wchar_t args[] = L"powershell.exe -NoLogo -NoProfile -NonInteractive -STA -EncodedCommand ";
  if (ARRAYSIZE(args) + encoded > 32767)
    goto done;
  command = calloc(ARRAYSIZE(args) + encoded, sizeof(*command));
  if (!command)
    goto done;
  wcscpy(command, args);
  if (!CryptBinaryToStringW((BYTE *)source, bytes, flags, command + wcslen(args), &encoded))
    goto done;
  STARTUPINFOW startup = {0};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process;
  if (CreateProcessW(shell, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                     NULL, NULL, &startup, &process)) {
    updater = process.hProcess;
    CloseHandle(process.hThread);
    started = TRUE;
  }
done:
  free(source);
  free(command);
  free(script);
  free(version);
  if (!started) {
    closeUpdateHandles();
    MessageBoxW(owner, L"PadClip could not start Windows PowerShell to check for updates.",
                L"PadClip updates", MB_ICONWARNING);
  }
  return started;
}
