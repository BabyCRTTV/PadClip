# PadClip

[Download PadClip for Windows x64](https://github.com/BabyCRTTV/PadClip/raw/refs/heads/main/8BitTrade-PadClip-Windows.zip)


Exit the running copy of PadClip, extract this ZIP and launch PadClip.exe.
Your saved controller shortcut is retained. The default is LB + RB pressed together.
Press and release the shortcut to start. Repeat to stop and save.
Change shortcut learns a new combination. Games also receive these inputs.

A brief on-screen banner appears after the encoder accepts the first frame.
The controller also pulses if it supports vibration. The status line
remains available in PadClip. Use Start recording and Stop and save in the
window to test capture independently of your controller.

Clips are saved as MP4 files under Videos\PadClip. The recorder captures
the primary display at native resolution, targeting its configured refresh
rate at the start of each clip. GPU desktop duplication and color conversion
feed the Windows H.264 encoder. Bitrate scales with resolution and refresh.
Actual frame delivery depends on game, GPU, encoder and disk throughput.
Audio comes from the default playback device. If that device cannot be
opened, the start banner says "video only". Game Bar is no longer required.
If an exclusive-fullscreen game produces a black clip or hides the banner,
try borderless windowed mode. For a failed start, the window shows the stage
and Windows error code. Exiting PadClip during recording finishes the clip.

Settings
Leave "Minimize to system tray" unchecked for normal taskbar behavior.
When checked, minimizing hides the window beside the clock. Click the
PadClip tray icon to restore, or right-click for Open and Exit. Windows
may put the icon in the hidden-icons menu. By default, closing the window exits. Enable "Close to system tray" to
keep PadClip and any active recording running when you click X. This setting
is independent of "Minimize to system tray".
On a fresh install, PadClip starts in the system tray at Windows sign-in.
Your recording shortcut is active immediately. Click the tray icon to open
the window. Clear "Start in the system tray when I sign in" to turn this off.
Upgrades keep your existing sign-in preference. Extract the app to its
permanent folder before launching it. After moving it, launch the new copy once to repair its sign-in path.
If Windows disabled the startup entry, the checkbox shows that state and can
open Windows Startup apps so you can enable PadClip there.

Opening PadClip centers a medium window on its monitor, sized to fit the available work area. The window supports resizing and maximizing. Its controls and text
adapt to monitor scaling, with stacked buttons in narrow windows and
vertical scrolling when space is limited.

Updates
Choose "Check for updates?" in Settings while PadClip is idle. If a newer
version is available, choose Yes to install and restart, or No to keep
using your copy. PadClip shows the changelog after it reopens. Older copies
without the button need the ZIP above once. Keep the extracted app in a
folder you can write to.

Only recording opens the display capture device, audio stream and encoder.

### Building an update

Put the new version number and its changelog in `release.txt`. Run
`python3 build.py` with the x64 MinGW-w64 tools on PATH. Publish the generated
ZIP and `update.json` together on `main` so installed copies can verify the download.
