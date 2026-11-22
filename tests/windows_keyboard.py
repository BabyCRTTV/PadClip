"""Exercise the shipped picker and the real background keyboard listener on Windows."""
import ctypes
import subprocess
import tempfile
import time
from zipfile import ZipFile

from windows_update import ROOT, UI, wait

CTRL, F8, F9 = 0x11, 0x77, 0x78


def key(vk, down):
    UI.keybd_event(vk, UI.MapVirtualKeyW(vk, 0), 0 if down else 2, 0)
    time.sleep(.035)


def tap(vk):
    key(vk, True)
    key(vk, False)


def caption(control):
    text = ctypes.create_unicode_buffer(512)
    UI.SendMessageW(control, 0x000D, len(text), ctypes.addressof(text))  # WM_GETTEXT also reads cross-process edits.
    return text.value


def picker(main):
    UI.PostMessageW(main, 0x111, 8, 0)
    result = wait(lambda: UI.FindWindowW("#32770", "Keyboard shortcut"), "keyboard picker")
    wait(lambda: UI.IsWindowVisible(result) and UI.GetDlgItem(result, 1000 + F8), "keyboard rendered")
    return result


def click(dialog, item):
    button = UI.GetDlgItem(dialog, item)
    assert button and UI.IsWindowEnabled(button), item
    UI.SendMessageW(button, 0xF5, 0, 0)
    if item in (1, 2):
        wait(lambda: not UI.IsWindow(dialog), "picker closed")


def run():
    with tempfile.TemporaryDirectory() as folder:
        with ZipFile(ROOT / "8BitTrade-PadClip-Windows.zip") as package:
            exe = package.extract("PadClip.exe", folder)
        for program in (exe, ROOT / "keyboard-test.exe"):
            app = subprocess.Popen([program, "--open"])
            try:
                main = wait(lambda: UI.FindWindowW("8BitTradePadClip", None), "main window")
                wait(lambda: UI.GetDlgItem(main, 8), "shortcut settings")
                dialog = picker(main)
                if program == exe:
                    click(dialog, 207)  # Clear draft.
                    for vk in (CTRL, F8):
                        click(dialog, 1000 + vk)
                    click(dialog, 205)  # Next step.
                    for vk in (CTRL, F9):
                        click(dialog, 1000 + vk)
                    click(dialog, 1)
                    dialog = picker(main)
                    click(dialog, 207)
                    click(dialog, 2)  # Cancel preserves saved combo.
                else:
                    summary = caption(UI.GetDlgItem(dialog, 202))
                    assert summary == "Ctrl + F8  then  Ctrl + F9", repr(summary)
                    click(dialog, 2)
                    box = UI.GetDlgItem(main, 7)
                    if not UI.SendMessageW(box, 0xF0, 0, 0):
                        click(main, 7)
                    UI.SendMessageW(main, 0x10, 0, 0)
                    assert not UI.IsWindowVisible(main)
                    record = UI.GetDlgItem(main, 3)
                    key(CTRL, True)
                    try:
                        tap(F9)
                        tap(F8)
                        tap(0x76)  # Wrong key resets the pending sequence.
                        tap(F9)
                        tap(F8)
                        time.sleep(1.4)
                        tap(F9)
                        assert caption(record) == "Start recording"
                        tap(F8)
                        key(F9, True)
                        wait(lambda: caption(record) == "Stop and save", "keyboard start from tray")
                        key(F9, True)
                        key(F9, False)
                        assert caption(record) == "Stop and save"
                        tap(F8)
                        tap(F9)
                        wait(lambda: caption(record) == "Start recording", "keyboard stop from tray")
                    finally:
                        key(CTRL, False)
                    assert not UI.IsWindowVisible(main)
                    UI.SendMessageW(main, 0x8003, 1, 0x202)
                    dialog = picker(main)
                    click(dialog, 207)
                    click(dialog, 1000 + F8)
                    click(dialog, 1)
                    key(F8, True)
                    key(F8, True)
                    key(F8, False)
                    assert caption(record) == "Stop and save"
                    tap(F8)
                    wait(lambda: caption(record) == "Start recording", "single key stops recording")
                    dialog = picker(main)
                    click(dialog, 207)
                    click(dialog, 1000 + F8)
                    click(dialog, 205)
                    click(dialog, 1000 + F8)
                    click(dialog, 1)
                    tap(F8)
                    assert caption(record) == "Start recording"
                    tap(F8)
                    assert caption(record) == "Stop and save"
                    tap(F8)
                    tap(F8)
                    wait(lambda: caption(record) == "Start recording", "double-tap sequence stops")
                    dialog = picker(main)
                    click(dialog, 207)
                    click(dialog, 1)
                    tap(F8)
                    tap(F8)
                    assert caption(record) == "Start recording"
                UI.PostMessageW(main, 0x111, 101, 0)
                assert app.wait(timeout=10) == 0
            finally:
                if app.poll() is None:
                    app.kill()
                    app.wait()
    print("Keyboard picker, persistence, cancel, background sequence, repeat and disable passed")


if __name__ == "__main__":
    run()
