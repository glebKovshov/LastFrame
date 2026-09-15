# Changelog

## Unreleased

- Added region selection overlay with monitor-bounded coordinates.
- Added output resolution, container, codec, preset, bitrate and file-size controls.
- Added export queue limits, unique reservations for concurrent clips and stale-temp cleanup.
- Added smoke coverage for region capture, audio fallback, concurrent saves and MP4/MKV/WebM.

## 0.1.0 — Windows MVP

- Qt Widgets settings window with dark/light themes and `#ED760E` accent.
- Single-instance behavior, system tray, Windows global hotkeys and JSON settings.
- Local FFmpeg segment recorder with H.264 NVENC, MP4 export and video-only fallback.
- Core unit tests and a real-desktop smoke target for Windows hardware validation.
- Portable Windows x64 packaging without an installer.
