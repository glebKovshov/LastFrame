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
- Native Windows DXGI Desktop Duplication backend: захват BGRA через D3D11 staging
  texture, crop выбранного монитора/региона, cursor overlay, постоянный FPS на
  статичном рабочем столе и raw-video pipe в FFmpeg; при ошибке возвращается к
  portable FFmpeg backend.
- Native Windows Graphics Capture fallback через C++/WinRT + D3D11: тот же
  monitor/region crop, cursor capture, постоянный FPS и raw-BGRA pipe; при
  ошибке возвращается к portable FFmpeg backend.
- Поиск аудиоустройств через FFmpeg: системный WASAPI output и микрофоны DirectShow,
  выбор устройства, независимая громкость и fallback system → microphone → video.
- Encoder fallback: при ошибке Auto/NVENC повторяется запуск с software H.264.
- Ошибки recorder/hotkey/update имеют стабильные технические коды для диагностики:
  `capture_failed`, `audio_device_lost`, `encoder_unavailable`, `export_failed`,
  `queue_overflow`, `hotkey_conflict`, `monitor_changed`.
- Ручная проверка GitHub Releases через `api.github.com`, без фоновой телеметрии или автообновления.
- Асинхронный FFmpeg capability probe в Advanced: Desktop Duplication/GDI,
  audio devices и доступные encoder profiles отображаются до запуска буфера.
- Qt-free `AudioMixer` core primitive: общий master clock, silence-fill,
  независимые уровни, mute transitions, resampling и clipping; покрыт unit-тестами
  и подключён к native Windows WASAPI loopback/microphone backend через mixed
  f32le pipe в encoder; отказ одного источника диагностируется без потери второго.
- Редактор глобальных хоткеев с проверкой дубликатов и откатом при конфликте регистрации.
- Наблюдатель мониторов с интервалом 500 ms: активный буфер безопасно останавливается при изменении дисплея.
- GitHub Actions для core tests и Windows portable artifact.

## Следующие обязательные срезы по ТЗ

1. HDR/DRM сценарии и явные capability/error states для недоступных или
   защищённых поверхностей; отдельная проверка SDR-конверсии HDR.
2. Device-loss recovery с попытками переподключения и auto-resume; текущий
   native WASAPI path уже передаёт mixed PCM в encoder, имеет silence-fill,
   fallback на portable FFmpeg audio и runtime mute/unmute microphone через
   global hotkey и tray action.
3. GPU scaler и прямой FFmpeg library encoder/muxer вместо процесса FFmpeg.
4. Region selector, DPI-aware coordinates, monitor watcher и overlay.
5. Интеграционные тесты на RTX 3070 и матрица Windows/Linux/macOS arm64.

До реализации следующих срезов UI честно показывает, что segment recorder
требует доступный FFmpeg; native DXGI/WGC и native WASAPI capability включаются
автоматически на Windows, а при недоступности остаются portable FFmpeg fallback.
