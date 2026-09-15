# LastFrame architecture

## Current Windows MVP

```text
GlobalHotkeyManager / MainWindow / SystemTray
              |
              v
      PortableSegmentRecorder
       |       |       |
       |       |       +--> bounded export queue
       |       +----------> FFmpeg mux/export + atomic rename
       +------------------> native DXGI capture -> raw BGRA pipe
                              native WASAPI -> AudioMixer -> f32le audio pipe
                              portable FFmpeg fallback
                              |
             Desktop Duplication -> D3D11 staging -> crop/cursor -> scale -> encoder
             FFmpeg ddagrab -> hwdownload -> scale -> encoder
             GDI gdigrab fallback
             portable FFmpeg WASAPI/DirectShow fallback

SettingsStore -> JSON schema + unknown-field preservation + recovery backup
Diagnostics   -> local text log without pixels/audio
MonitorEnumerator -> stable display list + 500 ms watcher
```

The UI owns no frame, audio sample, encoder packet, or export file data. It
only edits `Core::Settings` and reacts to recorder signals. The recorder owns
the FFmpeg process, segment directory and export jobs. The core library stays
Qt-free and contains the state machine, bounded queues, ring descriptors,
filename allocation, rate limiting and JSON settings model.

The core also contains a format-independent `AudioMixer` primitive. It accepts
timestamped PCM blocks from system and microphone sources, renders fixed-size
blocks on a shared master clock, fills missing sources with silence, applies
independent gain/mute state, resamples to the configured rate, and clamps the
mixed samples before a native audio backend hands them to the media layer.

## Data and failure boundaries

- Capture writes short immutable Matroska segments into the configured local
  temporary directory.
- Save snapshots only closed segments, then exports to `*.tmp` and atomically
  renames the final file. Existing user clips are never removed by recovery.
- Audio startup is progressive: native WASAPI mixer, portable FFmpeg system/mic
  inputs, then video-only. Missing sources are filled with silence by the mixer.
- Capture startup is progressive: native DXGI Desktop Duplication, portable
  FFmpeg Desktop Duplication, then GDI.
- Auto/NVENC encoding is progressive: hardware H.264, then software H.264.
- Monitor changes stop the active session; FPS is not silently changed.
- Logs contain lifecycle/error text and capability metadata only; no pixels,
  audio samples, window contents, or telemetry are collected.

## Planned platform seams

`PortableSegmentRecorder` is intentionally an adapter boundary. The next
production slice can replace its process adapter with an `ICaptureBackend`,
`IAudioBackend`, and `IMediaEncoder` implementation without changing the
settings schema, hotkey manager, tray actions, or core tests. The reusable
AudioMixer, native DXGI capture, and native WASAPI runtime capture are now in
the Windows path; Windows WGC, audio device-loss recovery with auto-resume,
Linux PipeWire/portal and macOS ScreenCaptureKit remain outside the validated
Windows MVP.
