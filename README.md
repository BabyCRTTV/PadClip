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
and Windows error code. Closing PadClip during recording finishes the clip.

Settings
Leave "Minimize to system tray" unchecked for normal taskbar behavior.
When checked, minimizing hides the window beside the clock. Click the
PadClip tray icon to restore, or right-click for Open and Exit. Windows
may put the icon in the hidden-icons menu. Closing the window exits.
Enable "Start minimized when I sign in to Windows" to launch the listener
using your chosen minimize behavior. Startup is off by default. Extract
the app to its permanent folder before enabling startup. If you move it,
turn the setting off and back on to update the executable path.

Only recording opens the display capture device, audio stream and encoder.
