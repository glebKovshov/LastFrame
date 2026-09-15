# LastFrame

LastFrame — локальная portable-утилита для сохранения последних секунд экрана по глобальному хоткею.

Текущий этап — Windows MVP: окно настроек, кольцевой буферный core, системный трей и подготовленный сегментный recorder через FFmpeg. Приложение не отправляет кадры, звук или логи в облако.

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

## MVP

В MVP входят:

- один выбранный монитор;
- буфер последних 5–300 секунд, по умолчанию 30;
- сохранение MP4 через короткие MKV-сегменты и атомарное переименование результата;
- системный трей, старт/пауза/стоп/очистка/сохранение;
- глобальные хоткеи по умолчанию `Ctrl+Shift+F10`, `Ctrl+Shift+F1`, `Ctrl+Shift+F7`, `Ctrl+Shift+F5`;
- JSON-настройки с миграционной точкой и сохранением неизвестных полей;
- bounded queues, rate limit 3 сохранения в секунду и cooldown 5 секунд;
- RU/EN-ready UI и тёмная/светлая тема с акцентом `#ED760E`.

Минимальная кроссплатформенная цель из ТЗ — Windows 10 22H2 и macOS 13+
на Apple Silicon (включая M1). В текущем срезе полностью проверен Windows
portable-путь; native macOS/Linux backends ещё не подключены.

Нативные DXGI/Desktop Duplication, WASAPI-микшер и backend’ы macOS/Linux остаются следующими вертикальными срезами. В текущем MVP внешний FFmpeg используется как изолированный media backend; это позволяет проверить сценарий на реальном Windows-железе до подключения прямых FFmpeg libraries.

## Публикация

Установщик не создаётся. Release-артефакты должны быть portable ZIP через [GitHub Releases](https://github.com/glebKovshov/LastFrame/releases). Проверка обновлений — ручная и выключена по умолчанию; endpoint — `https://api.github.com/repos/glebKovshov/LastFrame/releases/latest`.

## Документация

- [План реализации](docs/IMPLEMENTATION_STATUS_RU.md)
- [Changelog](CHANGELOG.md)
- [Contributing](CONTRIBUTING.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- Нормативное ТЗ хранится у заказчика в `TECHNICAL_SPECIFICATION_RU.md` и должно быть скопировано в `docs/` перед первым release-коммитом.

## Лицензия

Собственный код распространяется по [GPL-3.0-or-later](LICENSE). Для каждой release-сборки необходимо публиковать список лицензий и исходные уведомления используемых media-компонентов.
