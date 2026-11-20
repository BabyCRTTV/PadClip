"""Build the portable Windows package and its matching update feed with MinGW-w64."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parent
PACKAGE = ROOT / "8BitTrade-PadClip-Windows.zip"


def build():
    version, notes = (ROOT / "release.txt").read_text(encoding="utf-8").split("\n", 1)
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version) or not notes.strip():
        raise ValueError("release.txt must start with a numeric version followed by release notes")
    cc = os.environ.get("CC", "x86_64-w64-mingw32-gcc")
    cxx = os.environ.get("CXX", "x86_64-w64-mingw32-g++")
    rc = os.environ.get("WINDRES", "x86_64-w64-mingw32-windres")
    flags = ["-Os", "-D__CRT__NO_INLINE", "-DUNICODE", "-D_WIN32_WINNT=0x0A00"]
    with tempfile.TemporaryDirectory() as temp:
        out = Path(temp)
        objects = []
        for name in ("padclip.c", "updates.c", "capture.cpp"):
            obj = out / (name + ".o")
            compiler = cxx if name.endswith(".cpp") else cc
            subprocess.run([compiler, *flags, "-c", name, "-o", obj], cwd=ROOT, check=True)
            objects.append(obj)
        res = out / "resources.o"
        subprocess.run([rc, "--preprocessor", cc, "--preprocessor-arg=-E",
                        "--preprocessor-arg=-xc", "padclip.rc", "-O", "coff", "-o", res],
                       cwd=ROOT, check=True)
        exe = out / "PadClip.exe"
        subprocess.run([cxx, *objects, res, "-municode", "-mwindows", "-static", "-s",
                        "-Wl,--no-insert-timestamp", "-lcrypt32", "-ld3d11", "-ldxgi",
                        "-lmfreadwrite", "-lmfplat", "-lmfuuid", "-lole32", "-luuid",
                        "-lgdi32", "-lshell32", "-lshlwapi", "-ladvapi32", "-lxinput1_4",
                        "-o", exe], check=True)
        with ZipFile(PACKAGE, "w", ZIP_DEFLATED) as package:
            package.write(exe, "PadClip.exe")
            readme = ROOT / "README.md"
            package.write(readme if readme.exists() else ROOT / "Readme.txt", "Readme.txt")
            for name in ("padclip.c", "capture.cpp", "desktop.h", "updates.c", "updates.h",
                         "padclip.rc", "PadClip.ico", "updater.ps1", "release.txt", "build.py"):
                package.write(ROOT / name, name)
    feed = {"version": version, "sha256": hashlib.sha256(PACKAGE.read_bytes()).hexdigest()}
    (ROOT / "update.json").write_text(json.dumps(feed, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    build()
