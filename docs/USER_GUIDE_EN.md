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

The Audio page exposes detected microphones and independent system/microphone volume. The Windows path first uses native DXGI Desktop Duplication through a D3D11 staging/raw-BGRA pipe, then FFmpeg `ddagrab`, with GDI as the final fallback. Native WASAPI loopback and microphone capture are mixed by the shared master-clock `AudioMixer` into one 48 kHz stereo track; source loss emits `[audio_device_lost]` and falls back to an available source or portable FFmpeg audio. Microphone mute/unmute is available through the configured global hotkey and tray action. Device-loss recovery with automatic resume, Windows Graphics Capture fallback, Linux/macOS native backends, and direct FFmpeg library integration remain follow-up slices.
