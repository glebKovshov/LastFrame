# LastFrame — quick guide

## Start

Extract `LastFrame-windows-x64-portable.zip` to a writable directory and run `LastFrame.exe`. No installer is required. The portable archive includes FFmpeg and the Qt runtime.

The window can be closed to keep LastFrame in the system tray. The buffer is not started automatically: press “Начать буфер” in the current MVP UI.

## Quick workflow

1. On Capture, choose a monitor, full monitor, or a rectangular region.
2. Optionally set FPS and output resolution.
3. On Video, choose MP4, MKV, or WebM and an encoder profile.
4. Press “Начать буфер”.
5. Press “Сохранить клип” or `Ctrl+Shift+F10`.

Clips are saved to `Videos/LastFrame` by default. A final file is exposed only after a successful export and atomic rename.

## Privacy and updates

Capture, audio, clips, and diagnostics stay local. There is no telemetry. Update checks are manual and use the GitHub Releases API only when requested.

The Windows MVP uses FFmpeg `ddagrab` Desktop Duplication with a GDI fallback. WASAPI loopback depends on the supplied FFmpeg build; microphone mixing, Linux/macOS native backends, and direct FFmpeg library integration are planned follow-up slices.
