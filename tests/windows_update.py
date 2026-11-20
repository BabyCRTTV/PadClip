"""Exercise the shipped GUI and Windows PowerShell updater in a disposable Windows session."""
import ctypes
from ctypes import wintypes
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
UI = ctypes.WinDLL("user32", use_last_error=True)
for name, args, result in (
    ("FindWindowW", [wintypes.LPCWSTR, wintypes.LPCWSTR], wintypes.HWND),
    ("GetDlgItem", [wintypes.HWND, ctypes.c_int], wintypes.HWND),
    ("PostMessageW", [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM], wintypes.BOOL),
    ("IsWindowVisible", [wintypes.HWND], wintypes.BOOL),
    ("IsWindow", [wintypes.HWND], wintypes.BOOL),
    ("IsHungAppWindow", [wintypes.HWND], wintypes.BOOL),
    ("SetForegroundWindow", [wintypes.HWND], wintypes.BOOL),
    ("IsWindowEnabled", [wintypes.HWND], wintypes.BOOL),
    ("FindWindowExW", [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR], wintypes.HWND),
    ("GetWindowTextW", [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int], ctypes.c_int),
):
    func = getattr(UI, name)
    func.argtypes, func.restype = args, result


def wait(operation, description):
    until = time.monotonic() + 35
    while time.monotonic() < until:
        value = operation()
        if value:
            return value
        time.sleep(0.05)
    raise RuntimeError("Timed out waiting for " + description)


def dismiss(title, text, button=1):
    dialog = wait(lambda: UI.FindWindowW("#32770", title), title)
    child = None
    labels = []
    while child := UI.FindWindowExW(dialog, child, "Static", None):
        buf = ctypes.create_unicode_buffer(4096)
        UI.GetWindowTextW(child, buf, len(buf))
        labels.append(buf.value)
    assert any(text in label for label in labels), (title, labels)
    control = wait(lambda: UI.GetDlgItem(dialog, button), f"dialog button {button}")
    wait(lambda: UI.IsWindowVisible(dialog) and UI.IsWindowEnabled(control), "enabled confirmation")
    print("Dialog:", title, labels, "response", button, flush=True)
    # A message box can create its child controls before its modal loop starts.
    def respond():
        if not UI.IsWindow(dialog):
            return True
        UI.SetForegroundWindow(dialog)
        assert UI.PostMessageW(dialog, 0x111, button, control)
        if button == 1:
            UI.PostMessageW(dialog, 0x10, 0, 0)
        time.sleep(0.25)
        return not UI.IsWindow(dialog)
    wait(respond, f"dialog dismissal (hung={UI.IsHungAppWindow(dialog)})")


def check():
    window = wait(lambda: UI.FindWindowW("8BitTradePadClip", None), "PadClip window")
    button = UI.GetDlgItem(window, 6)
    wait(lambda: UI.IsWindowEnabled(button), "idle update button")
    assert UI.PostMessageW(window, 0x111, 6, button)


def replace_once(data, old, new):
    assert data.count(old) == 1 and len(old) == len(new)
    return data.replace(old, new)


def main():
    assert not UI.FindWindowW("8BitTradePadClip", None), "Close PadClip before testing."
    package = ROOT / "8BitTrade-PadClip-Windows.zip"
    feed = json.loads((ROOT / "update.json").read_text())
    assert hashlib.sha256(package.read_bytes()).hexdigest() == feed["sha256"]
    with ZipFile(package) as archive:
        shipped = archive.read("PadClip.exe")
        release = archive.read("release.txt")
        assert archive.read("updater.ps1") in shipped
    version, notes = release.decode().split("\n", 1)
    assert version == feed["version"]
    state = dict(feed)
    requests = []

    class Feed(BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            if self.path == "/update.json":
                payload = json.dumps(state).encode()
            elif self.path == "/" + package.name:
                payload = package.read_bytes()
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    server = ThreadingHTTPServer(("127.0.0.1", 0), Feed)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    # Point only the simulated older copy at this test's local feed.
    old_url = b"'https://raw.githubusercontent.com/BabyCRTTV/PadClip/main'"
    local_url = f"'http://127.0.0.1:{server.server_port}'".encode()
    fixture = replace_once(shipped, old_url, local_url.ljust(len(old_url)))
    older = b"0.0.0\n" + notes.encode()
    fixture = replace_once(fixture, release, older.ljust(len(release), b" "))
    try:
        with tempfile.TemporaryDirectory(prefix="PadClip QA ") as directory:
            app = Path(directory) / "PadClip.exe"
            app.write_bytes(fixture)
            old = subprocess.Popen([app, "--updated"])
            try:
                dismiss("What's new in PadClip 0.0.0", notes.splitlines()[0])
                state["version"] = "0.0.0"
                check()
                dismiss("PadClip updates", "up to date")
                state.update(feed)
                check()
                dismiss("PadClip updates", "Install the update", 7)
                assert requests.count("/" + package.name) == 0
                state["sha256"] = "0" * 64
                check()
                dismiss("PadClip updates", "Install the update", 6)
                dismiss("PadClip updates", "did not match")
                assert old.poll() is None and app.read_bytes() == fixture
                state.update(feed)
                check()
                dismiss("PadClip updates", "Install the update", 6)
                old.wait(timeout=35)
                dismiss("What's new in PadClip " + version, notes.splitlines()[0])
                assert app.read_bytes() == shipped
                wait(lambda: not list(app.parent.glob(".padclip-*")), "update cleanup")
            finally:
                window = UI.FindWindowW("8BitTradePadClip", None)
                if window:
                    UI.PostMessageW(window, 0x10, 0, 0)
                old.wait(timeout=10)
                wait(lambda: not UI.FindWindowW("8BitTradePadClip", None), "PadClip exit")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
