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
- WGC backend дополнительно прогнан end-to-end hardware smoke через
  `LASTFRAME_FORCE_WGC=1`: H.264 640×360 и AAC 48 kHz stereo успешно созданы.
- Поиск аудиоустройств через FFmpeg: системный WASAPI output и микрофоны DirectShow,
  выбор устройства, независимая громкость и fallback system → microphone → video.
- Encoder fallback: при ошибке Auto/NVENC повторяется запуск с software H.264.
- Capability probe и UI учитывают H.264 backend'ы NVIDIA NVENC, AMD AMF, Intel
  QSV и Apple VideoToolbox; экспорт показывает оценку размера и ограничивается
  `maxFileSizeMiB` самим FFmpeg до атомарного rename.
- `Auto` при ошибке последовательно пробует NVENC → AMF → QSV → VideoToolbox,
  затем software H.264; ручные backend'ы сохраняют отдельный software fallback.
- Перед запуском и export проверяются доступность, read-only и свободное место
  temporary/clips storage; при проблеме выдаётся `[disk_full]`, старые клипы не удаляются.
- Portable recorder на macOS использует FFmpeg `avfoundation`, на Linux/X11 —
  `x11grab`; Windows-only `wasapi/dshow/ddagrab` больше не формируются на других ОС.
  Нативные macOS ScreenCaptureKit и Linux PipeWire/portal остаются отдельными backend'ами.
- Windows DisplayConfig определяет активный HDR-монитор, список его помечает
  `HDR → SDR`, а UI явно предупреждает о потере HDR-диапазона; DRM не обходится.
- `RAM limit` вынесен в Advanced и сохраняется в JSON; завершённые portable-сегменты
  удерживаются в RAM до мягкого порога 70%, после чего остаются в локальном временном
  каталоге как disk spillover. Перед export RAM-сегменты материализуются без остановки capture.
- Snapshot экспортов удерживает reference-count на исходные сегменты; очистка буфера во время экспорта
  не удаляет их и не затрагивает текущий незакрытый writer-сегмент.
- Ошибки recorder/hotkey/update имеют стабильные технические коды для диагностики:
  `capture_failed`, `audio_device_lost`, `encoder_unavailable`, `export_failed`,
  `queue_overflow`, `hotkey_conflict`, `monitor_changed`.
- Ошибки показываются в отдельном окне с technical details, ссылкой на локальный
  log path, действиями copy diagnostics и restart capture.
- Ручная проверка GitHub Releases через `api.github.com`, без фоновой телеметрии или автообновления.
- Асинхронный FFmpeg capability probe в Advanced: Desktop Duplication/GDI,
  audio devices и доступные encoder profiles отображаются до запуска буфера.
- До запуска буфера Capture/Video показывают предупреждения capability: незавершённая
  проверка, недоступный ручной encoder/VP9, отсутствие WASAPI и FPS выше частоты
  выбранного монитора; fallback остаётся явным и диагностируемым.
- При наличии `scale_cuda` и NVENC recorder пробует GPU scaling через FFmpeg filter;
  если CUDA-устройство или filter не стартуют, незавершённый segment удаляется,
  запись продолжается через worker/software scaler без битого export.
- Добавлен отдельный `lastframe_notification_overlay_smoke`: на Windows он
  показывает overlay и проверяет `GetWindowDisplayAffinity == WDA_EXCLUDEFROMCAPTURE`.
- Первый запуск показывает Auto-профиль, ограничивает FPS частотой доступного монитора
  и не запускает capture без явного действия пользователя.
- Qt-free `AudioMixer` core primitive: общий master clock, silence-fill,
  независимые уровни, mute transitions, resampling и clipping; покрыт unit-тестами
  и подключён к native Windows WASAPI loopback/microphone backend через mixed
  f32le pipe в encoder; отказ одного источника диагностируется без потери второго.
- Редактор глобальных хоткеев с проверкой дубликатов и откатом при конфликте регистрации.
- Наблюдатель мониторов с интервалом 500 ms: активный буфер безопасно останавливается при изменении дисплея.
- Стабильные идентификаторы мониторов больше не зависят от разрешения и координат;
  недоступный выбранный монитор явно блокирует старт вместо тихого выбора первого.
- Windows lock/sleep/hibernate обрабатываются через power/session events: native
  session освобождается паузой, а после unlock/resume используется Auto resume с
  debounce 2 секунды.
- При потере native DXGI-сессии выполняются три попытки восстановления с backoff
  250/500/1000 ms, затем применяется WGC/portable fallback; процесс FFmpeg
  безопасно перезапускается без удаления старых клипов.
- При полной потере native WASAPI endpoint выполняются такие же три reattach-попытки,
  затем portable FFmpeg audio; потеря только одного источника остаётся degraded
  состоянием с продолжением второго источника и silence-fill.
- На macOS global hotkeys используют Carbon Event Hot Keys с абстрактными
  `CTRL/ALT/SHIFT/META` модификаторами; при отсутствии Accessibility/Input Monitoring
  permission показывается отдельный `hotkey_conflict` с рекомендацией разрешить доступ.
- Страница Notifications с настройками системных уведомлений, языка встроенных
  сообщений, четырёх углов, длительности и прозрачности click-through toast overlay
  с анимированным индикатором; на Windows overlay запрашивает
  `WDA_EXCLUDEFROMCAPTURE`, настройки сохраняются в JSON.
- Страница Storage позволяет выбрать каталог клипов и блокирует read-only, сетевые
  и съёмные носители; Capture предлагает Native, 720p, 1080p, 1440p, 4K и Custom.
- Иконка системного трея меняет цвет по состоянию: серый idle, зелёный recording,
  жёлтый paused/warning, красный error; из трея и Storage доступно открытие каталога клипов.
- Переключение языка интерфейса RU/EN в Advanced перестраивает окно и меню трея
  без перезапуска; динамические состояния, ошибки и служебные диалоги используют
  выбранную локаль, язык уведомлений настраивается отдельно.
- Геометрия монитора разделена на логическую Qt-координатную систему и физические
  пиксели native capture; custom region масштабируется по фактическому DPI/размеру
  дисплея перед DXGI/WGC/FFmpeg. Региональный smoke повторён с `QT_SCALE_FACTOR=1.5`.
- GitHub Actions для core tests и Windows portable artifact.
- GitHub Actions дополнительно компилирует Qt UI на `macos-14` arm64,
  включая Carbon hotkey integration; runtime capture smoke macOS пока не заявляется.

## Оставшиеся обязательные срезы по ТЗ

1. Интеграционная проверка исключения overlay на каждом capture backend'е и
   тесты DRM/HDR capability states; Windows display-affinity smoke уже добавлен.
2. GPU scaler для QSV/VAAPI/VideoToolbox и прямой FFmpeg library encoder/muxer
   вместо процесса FFmpeg; CUDA-путь с безопасным CPU fallback уже реализован.
3. Завершить ручную матрицу DPI для нескольких Windows-мониторов и native
   macOS ScreenCaptureKit/Linux PipeWire/portal backends.
4. Интеграционные тесты на RTX 3070 и полная матрица Windows/Linux/macOS arm64;
   macOS Carbon hotkeys добавлены, но native ScreenCaptureKit/аудио и Linux global
   hotkeys ещё требуют platform-specific implementation.

До реализации следующих срезов UI честно показывает, что segment recorder
требует доступный FFmpeg; native DXGI/WGC и native WASAPI capability включаются
автоматически на Windows, а при недоступности остаются portable FFmpeg fallback.
