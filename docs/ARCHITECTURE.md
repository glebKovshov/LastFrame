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
       +------------------> FFmpeg capture process
                              |
             Desktop Duplication ddagrab -> hwdownload -> scale -> encoder
             GDI gdigrab fallback
             WASAPI loopback when available
             DirectShow microphone fallback

SettingsStore -> JSON schema + unknown-field preservation + recovery backup
Diagnostics   -> local text log without pixels/audio
MonitorEnumerator -> stable display list + 500 ms watcher
```

The UI owns no frame, audio sample, encoder packet, or export file data. It
only edits `Core::Settings` and reacts to recorder signals. The recorder owns
the FFmpeg process, segment directory and export jobs. The core library stays
Qt-free and contains the state machine, bounded queues, ring descriptors,
filename allocation, rate limiting and JSON settings model.

## Data and failure boundaries

- Capture writes short immutable Matroska segments into the configured local
  temporary directory.
- Save snapshots only closed segments, then exports to `*.tmp` and atomically
  renames the final file. Existing user clips are never removed by recovery.
- Audio startup is progressive: system loopback, microphone, then video-only.
- Capture startup is progressive: Desktop Duplication, then GDI.
- Auto/NVENC encoding is progressive: hardware H.264, then software H.264.
- Monitor changes stop the active session; FPS is not silently changed.
- Logs contain lifecycle/error text and capability metadata only; no pixels,
  audio samples, window contents, or telemetry are collected.

## Planned platform seams

`PortableSegmentRecorder` is intentionally an adapter boundary. The next
production slice can replace its process adapter with an `ICaptureBackend`,
`IAudioBackend`, and `IMediaEncoder` implementation without changing the
settings schema, hotkey manager, tray actions, or core tests. Windows native
DXGI/WGC, full WASAPI master-clock mixing, Linux PipeWire/portal and macOS
ScreenCaptureKit remain outside the validated Windows MVP.
