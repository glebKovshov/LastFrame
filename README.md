# LastFrame

LastFrame — локальная portable-утилита для сохранения последних секунд экрана по глобальному хоткею.

Текущий этап — Windows MVP: окно настроек, кольцевой буферный core, системный трей и сегментный recorder через FFmpeg. На совместимых Windows-сборках используется Desktop Duplication (`ddagrab`), с автоматическим GDI fallback. Приложение не отправляет кадры, звук или логи в облако.

## Быстрый старт

Требуется:

- Windows 10 22H2 или Windows 11 x64;
- Visual Studio 2022 с workload **Desktop development with C++**;
- CMake 3.28+ и Ninja;
- Qt 6.8+ (компоненты Core, Gui, Widgets, Network);
- portable `ffmpeg.exe` рядом с приложением или в `PATH`.

```powershell
cmake --preset windows-debug
cmake --build build/windows --config Debug
ctest --test-dir build/windows -C Debug --output-on-failure
```

Если Qt ещё не установлен, можно проверить независимое ядро:

```powershell
cmake --preset core-only
cmake --build build/core --config Debug
ctest --preset core-only
```

Portable Windows ZIP собирается воспроизводимым скриптом:

```powershell
.\packaging\windows-portable.ps1 -QtRoot C:\Qt\6.8.3\msvc2022_64 -FfmpegBin C:\path\to\ffmpeg\bin
```

## MVP

В MVP входят:

- один выбранный монитор;
- full-monitor и прямоугольный region capture через полноэкранный overlay;
- Desktop Duplication через FFmpeg `ddagrab` с автоматическим GDI fallback;
- системный звук и микрофон через native WASAPI + общий master-clock mixer на Windows,
  с выбором устройства, независимой громкостью и безопасным fallback на FFmpeg;
- независимые output width/height и постоянный FPS в пределах частоты монитора;
- буфер последних 5–300 секунд, по умолчанию 30;
- сохранение MP4/MKV/WebM через короткие MKV-сегменты и атомарное переименование результата;
- Auto/H.264 NVENC/software H.264/VP9 profiles, bitrate и лимит размера файла;
- автоматический переход с недоступного NVENC на software H.264;
- системный трей, старт/пауза/стоп/очистка/сохранение;
- отдельная страница Notifications с отключаемыми toast/system уведомлениями;
- асинхронная capability-проверка FFmpeg до старта буфера с понятным списком capture/audio/encoder возможностей;
- глобальные хоткеи по умолчанию `Ctrl+Shift+F10`, `Ctrl+Shift+F1`, `Ctrl+Shift+F7`, `Ctrl+Shift+F5`;
- JSON-настройки с миграционной точкой и сохранением неизвестных полей;
- bounded queues, rate limit 3 сохранения в секунду и cooldown 5 секунд;
- RU/EN-ready UI и тёмная/светлая тема с акцентом `#ED760E`.

Минимальная кроссплатформенная цель из ТЗ — Windows 10 22H2 и macOS 13+
на Apple Silicon (включая M1). В текущем срезе полностью проверен Windows
portable-путь; native macOS/Linux backends ещё не подключены.

Native Windows DXGI Desktop Duplication уже подключён через D3D11 staging/raw-BGRA pipe, а при отказе DXGI используется Windows Graphics Capture с тем же crop/cursor/raw-video контрактом. Native WASAPI loopback и microphone capture сводятся через `AudioMixer` в f32le pipe. При недоступности native capture/audio автоматически используются portable FFmpeg backend’ы, включая `ddagrab`/`gdigrab` и доступный audio input. Mute/unmute microphone работает через global hotkey и tray action. Device-loss recovery с автоматическим resume, HDR/DRM capability states, прямые FFmpeg libraries и backend’ы macOS/Linux остаются следующими вертикальными срезами. Такой адаптер позволяет проверять сценарий на реальном Windows-железе без привязки UI к media implementation.

## Публикация

Установщик не создаётся. Release-артефакты должны быть portable ZIP через [GitHub Releases](https://github.com/glebKovshov/LastFrame/releases). Проверка обновлений — ручная и выключена по умолчанию; endpoint — `https://api.github.com/repos/glebKovshov/LastFrame/releases/latest`.

## Документация

- [План реализации](docs/IMPLEMENTATION_STATUS_RU.md)
- [Архитектура](docs/ARCHITECTURE.md)
- [Техническое задание](docs/TECHNICAL_SPECIFICATION_RU.md)
- [Changelog](CHANGELOG.md)
- [Contributing](CONTRIBUTING.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- Нормативное ТЗ хранится у заказчика в `TECHNICAL_SPECIFICATION_RU.md` и должно быть скопировано в `docs/` перед первым release-коммитом.

## Лицензия

Собственный код распространяется по [GPL-3.0-or-later](LICENSE). Для каждой release-сборки необходимо публиковать список лицензий и исходные уведомления используемых media-компонентов.
