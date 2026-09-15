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
- Region selector overlay с ограничением области выбранным монитором.
- Настраиваемые output width/height, FPS, контейнер MP4/MKV/WebM,
  encoder preset/bitrate и лимит размера файла.
- Ограниченная очередь export jobs и резервирование имён при параллельных сохранениях.
- Desktop Duplication через FFmpeg `ddagrab` с `hwdownload`/GPU desktop path и автоматическим GDI fallback.
- Поиск аудиоустройств через FFmpeg: системный WASAPI output и микрофоны DirectShow,
  выбор устройства, независимая громкость и fallback system → microphone → video.
- Encoder fallback: при ошибке Auto/NVENC повторяется запуск с software H.264.
- Ручная проверка GitHub Releases через `api.github.com`, без фоновой телеметрии или автообновления.
- Асинхронный FFmpeg capability probe в Advanced: Desktop Duplication/GDI,
  audio devices и доступные encoder profiles отображаются до запуска буфера.
- Редактор глобальных хоткеев с проверкой дубликатов и откатом при конфликте регистрации.
- Наблюдатель мониторов с интервалом 500 ms: активный буфер безопасно останавливается при изменении дисплея.
- GitHub Actions для core tests и Windows portable artifact.

## Следующие обязательные срезы по ТЗ

1. Прямой C++ Windows capture backend: DXGI Desktop Duplication, затем Windows
   Graphics Capture fallback; текущий portable backend уже использует FFmpeg
   `ddagrab` (DXGI Desktop Duplication) и GDI fallback.
2. Полный WASAPI loopback + microphone capture, master clock и AudioMixer
   поверх текущего device/fallback слоя.
3. GPU scaler и прямой FFmpeg library encoder/muxer вместо процесса FFmpeg.
4. Region selector, DPI-aware coordinates, monitor watcher и overlay.
5. Интеграционные тесты на RTX 3070 и матрица Windows/Linux/macOS arm64.

До реализации этих срезов UI честно показывает, что segment recorder требует
доступный FFmpeg и что native capture capability ещё не включена.
