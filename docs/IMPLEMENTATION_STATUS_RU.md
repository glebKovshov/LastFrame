# Статус реализации

## Рабочие решения

- Первая production-цель — Windows 11 x64.
- Минимальная Windows-цель — Windows 10 22H2.
- Минимальная Apple Silicon-цель — M1 с macOS 13 Ventura или новее.
- Дистрибуция — portable ZIP в GitHub Releases; установщик не создаётся.
- Лицензия проекта — GPL-3.0-or-later.
- Основной акцент интерфейса — `#ED760E`; базовые темы используют тёмный серый, белый и светло-серый.

## Готово в первом срезе

- CMake-проект C++23.
- Независимое ядро с state machine, bounded queue, ring buffer, rate limiter,
  filename allocator и атомарным JSON-хранилищем.
- Unit-тесты ядра.
- Qt Widgets UI с overview, страницами настроек и tray menu.
- Windows global hotkeys через `RegisterHotKey`.
- Segment recorder adapter с поиском portable `ffmpeg.exe`.
- GitHub Actions для core tests и Windows portable artifact.

## Следующие обязательные срезы по ТЗ

1. Прямой Windows capture backend: DXGI Desktop Duplication, затем Windows
   Graphics Capture fallback; исключить зависимость capture от UI thread.
2. WASAPI loopback + microphone capture, master clock и AudioMixer.
3. GPU scaler и прямой FFmpeg library encoder/muxer вместо процесса FFmpeg.
4. Region selector, DPI-aware coordinates, monitor watcher и overlay.
5. Интеграционные тесты на RTX 3070 и матрица Windows/Linux/macOS arm64.

До реализации этих срезов UI честно показывает, что segment recorder требует
доступный FFmpeg и что native capture capability ещё не включена.
