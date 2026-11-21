"""Run the packaged app in a disposable Windows account and exercise its sign-in entry."""
import ctypes
from ctypes import wintypes
from pathlib import Path
import subprocess
import tempfile
import winreg
from zipfile import ZipFile

from windows_update import UI, ROOT, dismiss, wait

RUN = r"Software\Microsoft\Windows\CurrentVersion\Run"
APPROVED = r"Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run"


def reg_set(key, kind, value):
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key) as handle:
        winreg.SetValueEx(handle, "PadClip", 0, kind, value)


def reg_get(key):
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key) as handle:
        return winreg.QueryValueEx(handle, "PadClip")[0]


def startup_window():
    main = wait(lambda: UI.FindWindowW("8BitTradePadClip", None), "PadClip")
    box = wait(lambda: UI.GetDlgItem(main, 5), "startup checkbox")
    return main, box


def close(app, main):
    UI.PostMessageW(main, 0x111, 101, 0)
    assert app.wait(timeout=10) == 0


def geometry(handle):
    rect = wintypes.RECT()
    assert UI.GetWindowRect(handle, ctypes.byref(rect))
    return rect


def exercise_layout(main):
    assert UI.GetWindowLongW(main, -16) & 0x40000  # WS_THICKFRAME
    UI.MoveWindow(main, 30, 30, 350, 280, True)
    first = geometry(UI.GetDlgItem(main, 1))
    record = geometry(UI.GetDlgItem(main, 3))
    assert record.top > first.top, "Narrow window must wrap the action buttons"
    UI.SendMessageW(main, 0x115, 7, 0)  # Scroll to bottom.
    client = wintypes.RECT()
    assert UI.GetClientRect(main, ctypes.byref(client))
    origin = wintypes.POINT()
    assert UI.ClientToScreen(main, ctypes.byref(origin))
    update = geometry(UI.GetDlgItem(main, 6))
    assert origin.y <= update.top < update.bottom <= origin.y + client.bottom
    UI.MoveWindow(main, 10, 10, 800, 640, True)
    assert geometry(UI.GetDlgItem(main, 1)).top == geometry(UI.GetDlgItem(main, 3)).top
    UI.SendMessageW(main, 0x112, 0xF030, 0)  # SC_MAXIMIZE
    assert UI.IsZoomed(main)


def main():
    for name, args in (("GetWindowRect", [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]),
                       ("GetClientRect", [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]),
                       ("ClientToScreen", [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]),
                       ("GetWindowLongW", [wintypes.HWND, ctypes.c_int]),
                       ("IsZoomed", [wintypes.HWND]),
                       ("MoveWindow", [wintypes.HWND, ctypes.c_int, ctypes.c_int,
                                       ctypes.c_int, ctypes.c_int, wintypes.BOOL])):
        getattr(UI, name).argtypes = args
    UI.SetProcessDPIAware()
    with tempfile.TemporaryDirectory(prefix="PadClip startup test ") as folder:
        with ZipFile(ROOT / "8BitTrade-PadClip-Windows.zip") as archive:
            exe = Path(archive.extract("PadClip.exe", folder))
            version, notes = archive.read("release.txt").decode().split("\n", 1)
        # Reproduce an enabled entry left behind by a moved portable copy.
        reg_set(RUN, winreg.REG_SZ, r'"C:\old folder\PadClip.exe" --startup')
        app = subprocess.Popen([exe, "--updated"])
        dismiss("What's new in PadClip " + version, notes.splitlines()[0])
        main, box = startup_window()
        assert UI.SendMessageW(box, 0xF0, 0, 0) == 1
        command = reg_get(RUN)
        assert command == f'"{exe}" --startup'
        exercise_layout(main)
        close(app, main)
        app = subprocess.Popen(command, cwd=Path.cwd())
        main, box = startup_window()
        wait(lambda: UI.IsIconic(main), "minimized sign-in launch")
        # A second invocation from Windows must not bring the first to the front.
        subprocess.run(command, check=True, timeout=10)
        assert UI.IsIconic(main)
        UI.SendMessageW(main, 0x8004, 0, 0)
        UI.SendMessageW(box, 0xF5, 0, 0)
        try:
            reg_get(RUN)
        except FileNotFoundError:
            pass
        else:
            raise AssertionError("Disabling startup left its entry behind")
        UI.SendMessageW(box, 0xF5, 0, 0)
        assert reg_get(RUN) == command
        close(app, main)
        disabled = bytes([3]) + bytes(11)
        reg_set(APPROVED, winreg.REG_BINARY, disabled)
        app = subprocess.Popen([exe])
        main, box = startup_window()
        wait(lambda: UI.IsWindowVisible(main), "startup status")
        assert UI.SendMessageW(box, 0xF0, 0, 0) == 0
        label = ctypes.create_unicode_buffer(120)
        UI.GetWindowTextW(box, label, len(label))
        assert "disabled in Windows" in label.value
        assert reg_get(APPROVED) == disabled
        close(app, main)


if __name__ == "__main__":
    main()
