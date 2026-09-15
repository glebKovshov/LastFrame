# Changelog

## Unreleased

- Added region selection overlay with monitor-bounded coordinates.
- Added output resolution, container, codec, preset, bitrate and file-size controls.
- Added export queue limits, unique reservations for concurrent clips and stale-temp cleanup.
- Added smoke coverage for region capture, audio fallback, concurrent saves and MP4/MKV/WebM.
- Added Desktop Duplication capture through FFmpeg `ddagrab` with automatic GDI fallback.
- Added native Windows DXGI Desktop Duplication through D3D11 staging and a
  constant-FPS raw-BGRA pipe, including cursor overlay and automatic fallback
  to the portable FFmpeg capture path.
- Added a native Windows Graphics Capture fallback using C++/WinRT and D3D11,
  preserving monitor/region crop, cursor capture and constant-FPS raw-BGRA input.
- Added the Qt-free master-clock AudioMixer primitive with silence fill,
  independent gain/mute, resampling, clipping and unit coverage.
- Connected native Windows WASAPI loopback and microphone capture to the
  AudioMixer, with mixed 48 kHz stereo f32le piped to the encoder and source
  degradation/fallback diagnostics.
- Added runtime microphone mute/unmute through the global hotkey and tray menu;
  native capture changes gain without restarting the session.
- Added manual GitHub Releases update check, editable global hotkeys and monitor-change watcher.
- Added microphone device discovery through DirectShow, system/microphone volume controls and audio fallback chain.
- Added automatic NVENC to software H.264 encoder fallback.
- Added asynchronous FFmpeg capability diagnostics before starting the buffer.
- Added stable technical error codes for capture, audio, export, queue, hotkey and monitor failures.
- Added stable monitor identifiers independent of geometry changes and stopped silently
  falling back to the first display when a selected monitor is unavailable.
- Added a Notifications settings page plus an in-app non-interactive toast overlay
  toggle; notification preferences are persisted in the existing JSON schema.
- Added encoder capability entries for NVIDIA NVENC, AMD AMF, Intel QSV and Apple
  VideoToolbox, plus an estimated export size and FFmpeg output-size guard.
- Added a click-through corner notification overlay with animated accent/error
  indicator; on supported Windows capture APIs it requests `WDA_EXCLUDEFROMCAPTURE`.
- Added Windows lock/sleep/hibernate lifecycle handling: capture sessions pause on
  suspend/lock and use the existing two-second Auto resume path after unlock/resume.
- Added an actionable error dialog with technical details, local log path, copy
  diagnostics and capture restart actions.
- Added a hardware smoke override (`LASTFRAME_FORCE_WGC=1`) used to validate the
  Windows Graphics Capture path end-to-end on the development machine.

## 0.1.0 — Windows MVP

- Qt Widgets settings window with dark/light themes and `#ED760E` accent.
- Single-instance behavior, system tray, Windows global hotkeys and JSON settings.
- Local FFmpeg segment recorder with H.264 NVENC, MP4 export and video-only fallback.
- Core unit tests and a real-desktop smoke target for Windows hardware validation.
- Portable Windows x64 packaging without an installer.
