# Changelog

## Unreleased

- Added hybrid RAM retention for completed segments with a 70% soft limit,
  disk spillover and export-time materialization without stopping capture.
- RAM-segment materialization now runs on a bounded QtConcurrent worker path,
  keeping Save hotkeys and the UI thread free from large synchronous writes.
- Export snapshots retain their source segments until completion; concurrent
  clear-buffer actions cannot delete an active snapshot or the current writer file.
- Added a first-run Auto profile dialog that keeps capture stopped until the user
  explicitly starts the buffer.
- Added notification language, corner, duration and opacity settings with JSON validation.
- Added complete RU/EN interface switching with immediate window/tray retranslation
  and a persistent interface-language setting.
- Added logical-to-physical monitor geometry mapping for high-DPI custom regions;
  native DXGI/WGC and portable capture now receive physical pixel rectangles.
- Added local clip-directory selection, temporary-segment visibility, log opening and
  tray/Storage actions for opening the clips directory.
- Added state-colored tray icon: gray idle, green recording, yellow paused/warning,
  red error.
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
- Auto encoder selection now walks hardware H.264 backends in capability order
  before falling back to software encoding.
- Added local storage checks before buffer start and clip export, with free-space
  information and `[disk_full]` diagnostics instead of touching existing clips.
- Added portable capture input seams for macOS `avfoundation` and Linux X11
  `x11grab`; non-Windows builds no longer emit Windows-only audio/capture inputs.
- Extended core settings coverage for all supported H.264 backend identifiers.
- Added Windows DisplayConfig HDR capability detection and an explicit HDR → SDR
  warning in the Capture UI; protected surfaces remain OS-controlled.
- Exposed the persisted RAM limit in Advanced so the portable disk-spillover
  policy is visible and adjustable instead of silently using a hidden default.
- Added native DXGI device-loss retries with 250/500/1000 ms backoff before
  switching to Windows Graphics Capture and then portable FFmpeg.
- Added matching native WASAPI endpoint reattach retries before switching to
  portable FFmpeg audio; one-source degradation still keeps the other source alive.
- Added a hardware smoke override (`LASTFRAME_FORCE_WGC=1`) used to validate the
  Windows Graphics Capture path end-to-end on the development machine.

## 0.1.0 — Windows MVP

- Qt Widgets settings window with dark/light themes and `#ED760E` accent.
- Single-instance behavior, system tray, Windows global hotkeys and JSON settings.
- Local FFmpeg segment recorder with H.264 NVENC, MP4 export and video-only fallback.
- Core unit tests and a real-desktop smoke target for Windows hardware validation.
- Portable Windows x64 packaging without an installer.
