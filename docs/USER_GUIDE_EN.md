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

The Audio page exposes detected microphones and independent system/microphone volume. The Windows path first uses native DXGI Desktop Duplication through a D3D11 staging/raw-BGRA pipe, then Windows Graphics Capture, then FFmpeg `ddagrab`, with GDI as the final fallback. Native WASAPI loopback and microphone capture are mixed by the shared master-clock `AudioMixer` into one 48 kHz stereo track; source loss emits `[audio_device_lost]` and falls back to an available source or portable FFmpeg audio. Microphone mute/unmute is available through the configured global hotkey and tray action. Device-loss recovery with automatic resume and HDR/DRM capability states are implemented in the Windows slice; native macOS/Linux capture and direct FFmpeg library integration remain follow-up production slices.

The Notifications page controls system notifications and the click-through corner overlay, including RU/EN notification language, all four corners, duration, and opacity. The overlay is short-lived and requests `WDA_EXCLUDEFROMCAPTURE` on supported Windows capture APIs.

The full interface language is selected on Advanced → Interface language. Switching to RU or EN rebuilds the window and tray menu immediately and persists the choice in `settings.json`; notification language remains independently configurable.

Custom-region coordinates are stored logically and converted to the monitor's physical pixels before capture, so Windows scaling is accounted for. A manual release check is still required for multi-monitor setups with different DPI values.

Storage lets the user choose, open, and clean the local clip directory. Completed segments stay in RAM up to the 70% soft limit and spill over to the local temporary directory when needed.
