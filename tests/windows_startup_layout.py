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


class TrayIconID(ctypes.Structure):
    _fields_ = [("size", wintypes.DWORD), ("window", wintypes.HWND),
                ("id", wintypes.UINT), ("guid", ctypes.c_ubyte * 16)]


def exercise_tray_launch(exe, command, minimize_to_tray, show_mode):
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\8BitTrade\PadClip") as key:
        winreg.SetValueEx(key, "Tray", 0, winreg.REG_DWORD, int(minimize_to_tray))
    launch = subprocess.STARTUPINFO()
    launch.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    launch.wShowWindow = show_mode
    app = subprocess.Popen(command, startupinfo=launch)
    try:
        main, box = startup_window()
        icon = TrayIconID(ctypes.sizeof(TrayIconID), main, 1)
        rect = wintypes.RECT()
        get_rect = ctypes.windll.shell32.Shell_NotifyIconGetRect
        get_rect.argtypes = [ctypes.POINTER(TrayIconID), ctypes.POINTER(wintypes.RECT)]

        def registered():
            assert not UI.IsWindowVisible(main), "Sign-in opened the PadClip window"
            return get_rect(ctypes.byref(icon), ctypes.byref(rect)) >= 0
        wait(registered, "PadClip icon in Explorer's notification area")
        # The controller polling loop must keep running with the window hidden.
        status = wait(lambda: UI.FindWindowExW(main, None, "Static",
                      "Waiting for an Xbox-compatible controller."), "listener status")
        marker = ctypes.create_unicode_buffer("Testing a fresh poll")
        UI.SendMessageW(status, 0x0C, 0, ctypes.addressof(marker))
        def polled_again():
            text = ctypes.create_unicode_buffer(128)
            UI.GetWindowTextW(status, text, len(text))
            return text.value.startswith("Waiting for an Xbox")
        wait(polled_again, "controller poll while hidden")
        subprocess.run(command, check=True, timeout=10)
        assert not UI.IsWindowVisible(main)
        if show_mode == 3:
            subprocess.run([exe], check=True, timeout=10)
        else:
            UI.PostMessageW(main, 0x8003, 1, 0x202)
        wait(lambda: UI.IsWindowVisible(main) and not UI.IsIconic(main), "explicit open")
        assert_medium(main)
        print(f"Tray launch passed: manual minimize={minimize_to_tray}, shell mode={show_mode}")
        close(app, main)
    finally:
        if app.poll() is None:
            app.kill()
            app.wait()


def geometry(handle):
    rect = wintypes.RECT()
    assert UI.GetWindowRect(handle, ctypes.byref(rect))
    return rect


def assert_medium(main):
    assert UI.IsWindowVisible(main) and not UI.IsIconic(main) and not UI.IsZoomed(main)
    area = wintypes.RECT()
    assert UI.SystemParametersInfoW(48, 0, ctypes.byref(area), 0)  # SPI_GETWORKAREA
    frame = geometry(main)
    assert abs(frame.left + frame.right - area.left - area.right) <= 2
    assert abs(frame.top + frame.bottom - area.top - area.bottom) <= 2
    assert frame.right - frame.left <= round((area.right - area.left) * .8) + 1
    assert frame.bottom - frame.top <= round((area.bottom - area.top) * .8) + 1
    return frame.left, frame.top, frame.right, frame.bottom


def exercise_medium_reopen(exe):
    for mode in (1, 2, 3):
        launch = subprocess.STARTUPINFO()
        launch.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        launch.wShowWindow = mode
        app = subprocess.Popen([exe], startupinfo=launch)
        try:
            main, _ = startup_window()
            wait(lambda: UI.IsWindowVisible(main), "explicit launch")
            expected = assert_medium(main)
            close_box = UI.GetDlgItem(main, 7)
            if not UI.SendMessageW(close_box, 0xF0, 0, 0):
                UI.SendMessageW(close_box, 0xF5, 0, 0)
            UI.SendMessageW(main, 0x112, 0xF030, 0)
            assert UI.IsZoomed(main)
            UI.SendMessageW(main, 0x10, 0, 0)
            assert not UI.IsWindowVisible(main)
            UI.SendMessageW(main, 0x8003, 1, 0x202)
            assert assert_medium(main) == expected
            UI.MoveWindow(main, 10, 10, 350, 280, True)
            subprocess.run([exe], check=True, timeout=10)
            wait(lambda: geometry(main).left == expected[0], "recenter")
            assert assert_medium(main) == expected
            close(app, main)
            print(f"Medium launch, restore from maximized tray, and shortcut reopen passed: {mode}")
        finally:
            if app.poll() is None:
                app.kill()
                app.wait()


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
        exercise_medium_reopen(exe)
        for minimize_to_tray in (False, True):
            for shell_mode in (1, 2, 3):  # Normal, minimized, maximized.
                exercise_tray_launch(exe, command, minimize_to_tray, shell_mode)
        app = subprocess.Popen([exe])
        main, box = startup_window()
        wait(lambda: UI.IsWindowVisible(main), "manual launch")
        UI.SendMessageW(box, 0xF5, 0, 0)
        try:
            reg_get(RUN)
        except FileNotFoundError:
            pass
        else:
            raise AssertionError("Disabling startup left its entry behind")
        close(app, main)
        subprocess.run(command, check=True, timeout=10)  # Stale sign-in after opt-out.
        assert not UI.FindWindowW("8BitTradePadClip", None)
        app = subprocess.Popen([exe])
        main, box = startup_window()
        wait(lambda: UI.IsWindowVisible(main), "manual launch after opt-out")
        assert UI.SendMessageW(box, 0xF0, 0, 0) == 0
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
