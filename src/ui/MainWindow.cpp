#include "ui/MainWindow.h"
#include "ui/NotificationOverlay.h"
#include "ui/RegionSelector.h"
#include "platform/AudioDeviceEnumerator.h"

#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDesktopServices>
#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QUrl>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QVBoxLayout>
#include <QWidget>

#if defined(Q_OS_WIN)
#include <Windows.h>
#include <wtsapi32.h>
#endif

#include <algorithm>
#include <functional>

namespace LastFrame::UI {
namespace {

constexpr auto accent = "#ED760E";

QLabel* heading(const QString& text, QWidget* parent = nullptr) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("pageHeading"));
    return label;
}

QLabel* description(const QString& text, QWidget* parent = nullptr) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setObjectName(QStringLiteral("description"));
    return label;
}

QWidget* card(const QString& title, const QString& body, QWidget* parent = nullptr) {
    auto* box = new QGroupBox(parent);
    box->setTitle(title);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->addWidget(description(body, box));
    return box;
}

QString locateFfmpegForUi() {
    const QString adjacent = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ffmpeg.exe"));
    return QFileInfo::exists(adjacent) ? adjacent : QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QString localizedUiText(const QString& text, const QString& language) {
    struct Translation {
        const char* russian;
        const char* english;
    };
    static const Translation translations[] = {
        {"Сохраняйте последние секунды игры", "Save the last seconds of your game"},
        {"Overview", "Overview"},
        {"Обзор", "Overview"},
        {"Capture", "Capture"},
        {"Захват", "Capture"},
        {"Video", "Video"},
        {"Видео", "Video"},
        {"Audio", "Audio"},
        {"Аудио", "Audio"},
        {"Hotkeys", "Hotkeys"},
        {"Горячие клавиши", "Hotkeys"},
        {"Storage", "Storage"},
        {"Хранилище", "Storage"},
        {"Notifications", "Notifications"},
        {"Уведомления", "Notifications"},
        {"Advanced", "Advanced"},
        {"Дополнительно", "Advanced"},
        {"About", "About"},
        {"О программе", "About"},
        {"Локальный кольцевой буфер для быстрого сохранения игровых моментов.", "Local ring buffer for quickly saving game moments."},
        {"Состояние буфера", "Buffer status"},
        {"Выключен", "Off"},
        {"Нажмите «Начать буфер», чтобы начать захват.", "Press “Start buffer” to begin capture."},
        {"Начать буфер", "Start buffer"},
        {"Остановить", "Stop"},
        {"Пауза", "Pause"},
        {"Продолжить", "Resume"},
        {"Сохранить клип", "Save clip"},
        {"Очистить", "Clear"},
        {"Монитор", "Monitor"},
        {"Один выбранный монитор, cursor capture включён по умолчанию.", "One selected monitor; cursor capture is enabled by default."},
        {"Буфер", "Buffer"},
        {"Последние N секунд; сегменты ограничиваются по времени и очереди.", "Last N seconds; segments are bounded by time and queue limits."},
        {"Приватность", "Privacy"},
        {"Кадры, звук и диагностика остаются на этом компьютере.", "Frames, audio, and diagnostics stay on this computer."},
        {"Активен", "Active"},
        {"Новые кадры временно не поступают; накопленные сегменты доступны для сохранения.", "New frames are temporarily paused; accumulated segments are available to save."},
        {"Выберите источник и безопасные параметры захвата.", "Choose the source and safe capture parameters."},
        {"Весь монитор", "Full monitor"},
        {"Прямоугольная область", "Custom region"},
        {"Источник", "Source"},
        {"Выбрать область", "Choose region"},
        {"Регион", "Region"},
        {"Показывать курсор", "Show cursor"},
        {"Автоматически продолжать после изменения монитора", "Auto resume after monitor changes"},
        {"Разрешение вывода", "Output resolution"},
        {"Ширина вывода", "Output width"},
        {"Высота вывода", "Output height"},
        {"Регион ограничивается выбранным монитором. HDR-мониторы помечаются как HDR → SDR: защищённый контент не обходится, а HDR-диапазон может быть потерян. Параметры применяются при следующем запуске буфера.", "The region is constrained to the selected monitor. HDR monitors are marked HDR → SDR: protected content is not bypassed, and HDR range may be lost. Parameters apply on the next buffer start."},
        {"MVP использует MP4 как основной формат и аппаратный H.264 NVENC на NVIDIA.", "The MVP uses MP4 by default and hardware H.264 NVENC on NVIDIA."},
        {"Контейнер", "Container"},
        {"Кодек", "Codec"},
        {"Пресет", "Preset"},
        {"Custom bitrate", "Custom bitrate"},
        {"Лимит файла", "File size limit"},
        {"Оценка размера", "Estimated size"},
        {"MP4/MKV используют H.264, WebM — VP9. Auto выбирает доступный hardware H.264 через capability probe и сохраняет fallback через software encoder.", "MP4/MKV use H.264, WebM uses VP9. Auto selects available hardware H.264 through the capability probe and falls back to the software encoder."},
        {"выше лимита, экспорт будет остановлен", "above the limit; export will stop"},
        {"в пределах лимита", "within the limit"},
        {"Системный звук подключается через WASAPI loopback, микрофон — через доступный локальный audio backend. Источники не покидают компьютер.", "System audio uses WASAPI loopback and the microphone uses an available local audio backend. Sources never leave the computer."},
        {"Системный звук", "System audio"},
        {"Микрофон", "Microphone"},
        {"Устройство system", "System device"},
        {"Устройство microphone", "Microphone device"},
        {"Громкость system", "System volume"},
        {"Громкость microphone", "Microphone volume"},
        {"48 kHz, stereo; AAC для MP4 и Opus для WebM. Если WASAPI недоступен, LastFrame продолжит с микрофоном или видео и запишет причину в диагностику.", "48 kHz, stereo; AAC for MP4 and Opus for WebM. If WASAPI is unavailable, LastFrame continues with microphone or video and records the reason in diagnostics."},
        {"Глобальные комбинации работают поверх игры и проверяются до регистрации.", "Global shortcuts work over games and are validated before registration."},
        {"Сохранить", "Save"},
        {"Старт/стоп", "Start/stop"},
        {"Mute microphone", "Mute microphone"},
        {"Применить хоткеи", "Apply hotkeys"},
        {"Формат: Ctrl+Shift+F10. Пустое поле отключает действие; одинаковые комбинации запрещены.", "Format: Ctrl+Shift+F10. An empty field disables an action; duplicate combinations are not allowed."},
        {"Готовые клипы не удаляются автоматически. Незавершённые временные сегменты находятся в каталоге приложения.", "Completed clips are never deleted automatically. Incomplete temporary segments are kept in the application directory."},
        {"Выбрать каталог клипов", "Choose clips directory"},
        {"Открыть каталог клипов", "Open clips directory"},
        {"Каталог недоступен", "Directory unavailable"},
        {"Носитель недоступен или ещё не готов.", "The storage is unavailable or not ready yet."},
        {"Носитель доступен только для чтения.", "The storage is read-only."},
        {"На носителе должно быть не менее 64 MiB свободного места.", "The storage must have at least 64 MiB free."},
        {"Выберите каталог на локальном фиксированном диске; сетевые и съёмные носители не поддерживаются.", "Choose a directory on a local fixed disk; network and removable media are not supported."},
        {"Очистить временные сегменты и буфер", "Clear temporary segments and buffer"},
        {"Можно выбрать только локальный фиксированный диск. Сетевые и съёмные носители блокируются; временные сегменты остаются в локальном каталоге приложения.", "Only a local fixed disk can be selected. Network and removable media are blocked; temporary segments stay in the local application directory."},
        {"Настройте локальные уведомления LastFrame. Звуковая обратная связь не используется.", "Configure local LastFrame notifications. Audio feedback is not used."},
        {"Показывать уведомления", "Show notifications"},
        {"Системные уведомления", "System notifications"},
        {"Показывать угловой overlay", "Show corner overlay"},
        {"Toast overlay", "Toast overlay"},
        {"Русский", "Russian"},
        {"English", "English"},
        {"Язык уведомлений", "Notification language"},
        {"Сверху справа", "Top right"},
        {"Сверху слева", "Top left"},
        {"Снизу справа", "Bottom right"},
        {"Снизу слева", "Bottom left"},
        {"Положение overlay", "Overlay position"},
        {"Длительность", "Duration"},
        {"Непрозрачность", "Opacity"},
        {"Системные toast-сообщения выводятся через трей. Overlay не перехватывает мышь и не используется как игровой HUD; на Windows запрашивается исключение из поддерживаемого захвата экрана.", "System toasts are shown through the tray. The overlay is click-through and is not a game HUD; on Windows, capture exclusion is requested when supported."},
        {"Длина буфера", "Buffer duration"},
        {"RAM limit", "RAM limit"},
        {"Тёмная", "Dark"},
        {"Светлая", "Light"},
        {"Тема", "Theme"},
        {"Язык интерфейса", "Interface language"},
        {"Проверить обновления", "Check for updates"},
        {"Скопировать диагностику", "Copy diagnostics"},
        {"Открыть локальный лог", "Open local log"},
        {"Телеметрия отключена. Проверка обновлений будет ручной через GitHub Releases и по умолчанию отключена.", "Telemetry is disabled. Update checks are manual through GitHub Releases and disabled by default."},
        {"FFmpeg: проверка capability…", "FFmpeg: checking capabilities…"},
        {"FFmpeg: поиск capability при старте", "FFmpeg: looking for capabilities at startup"},
        {"Каталог сегментов: %1", "Segment directory: %1"},
        {"About LastFrame", "About LastFrame"},
        {"Open-source локальный instant replay recorder. Никаких аккаунтов, облака или фоновой телеметрии.", "Open-source local instant replay recorder. No accounts, cloud, or background telemetry."},
        {"Лицензия: GPL-3.0-or-later. Portable release публикуется через GitHub Releases без установщика.", "License: GPL-3.0-or-later. Portable releases are published through GitHub Releases without an installer."},
        {"Одинаковые хоткеи использовать нельзя.", "Duplicate hotkeys are not allowed."},
        {"Не удалось зарегистрировать хоткеи; настройки откатились.", "Hotkeys could not be registered; settings were reverted."},
        {"Хоткеи применены.", "Hotkeys applied."},
        {"Нельзя начать буфер: монитор не выбран.", "Cannot start buffer: no monitor is selected."},
        {"Выбранный монитор недоступен. Выберите доступный монитор заново.", "The selected monitor is unavailable. Choose an available monitor again."},
        {"Выбран custom region, но область не задана или слишком мала.", "Custom region is selected, but the region is missing or too small."},
        {"Выходное разрешение должно быть чётным для выбранного YUV420 профиля.", "Output dimensions must be even for the selected YUV420 profile."},
        {"Сначала выберите монитор.", "Select a monitor first."},
        {"Регион слишком мал.", "The region is too small."},
        {"Проверка…", "Checking…"},
        {"LastFrame — ошибка", "LastFrame — error"},
        {"Проверьте настройки захвата или перезапустите сессию. Старые сохранённые клипы не затрагиваются.", "Check capture settings or restart the session. Existing saved clips are not affected."},
        {"Технический код и подробности записаны в локальный лог:\n%1", "The technical code and details were written to the local log:\n%1"},
        {"Перезапустить захват", "Restart capture"},
        {"Закрыть", "Close"},
        {"Начать/остановить буфер", "Start/stop buffer"},
        {"Пауза/продолжение", "Pause/resume"},
        {"Очистить буфер", "Clear buffer"},
        {"Открыть папку клипов", "Open clips folder"},
        {"Настройки", "Settings"},
        {"Выход", "Quit"},
        {"FFmpeg не найден. Положите ffmpeg.exe рядом с LastFrame.exe или добавьте его в PATH.", "FFmpeg was not found. Place ffmpeg.exe next to LastFrame.exe or add it to PATH."},
        {"capture backend не найден", "capture backend not found"},
        {"доступен", "available"},
        {"нет", "none"},
        {"Начальная настройка готова. Профиль Auto использует выбранный монитор, безопасный FPS и аппаратный H.264 с fallback.", "Initial setup is ready. The Auto profile uses the selected monitor, a safe FPS, and hardware H.264 with fallback."},
        {"Захват не запускается автоматически. Можно оставить Auto или открыть страницы настроек для точной конфигурации.", "Capture does not start automatically. Keep Auto or open Settings for detailed configuration."},
        {"Включить микрофон", "Unmute microphone"},
        {"Выключить микрофон", "Mute microphone"},
        {"Добро пожаловать в LastFrame", "Welcome to LastFrame"},
        {"Оставить Auto", "Keep Auto"},
        {"Открыть настройки", "Open settings"},
        {"Позже", "Later"},
        {"Буфер запущен", "Buffer started"},
        {"Буфер остановлен", "Buffer stopped"},
        {"Микрофон выключен", "Microphone muted"},
        {"Микрофон включён", "Microphone unmuted"},
        {"Установлена актуальная версия.", "You are up to date."},
        {"Опубликованных releases пока нет.", "No releases have been published yet."},
        {"Диагностика скопирована в буфер обмена.", "Diagnostics copied to clipboard."},
        {"Буфер очищен; сохранённые клипы не затронуты.", "Buffer cleared; saved clips were not touched."},
        {"Клип сохранён", "Clip saved"},
        {"LastFrame свёрнут в трей.", "LastFrame was minimized to the tray."},
    };
    const bool english = language.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0;
    for (const auto& translation : translations) {
        const QString russian = QString::fromUtf8(translation.russian);
        const QString translated = QString::fromUtf8(translation.english);
        if (russian == translated) {
            continue;
        }
        if (text == russian) {
            return english ? translated : russian;
        }
        if (text == translated) {
            return english ? translated : russian;
        }
    }
    if (english) {
        if (text.startsWith(QStringLiteral("Клипы: "))) return QStringLiteral("Clips: ") + text.mid(QStringLiteral("Клипы: ").size());
        if (text.startsWith(QStringLiteral("Временные сегменты: "))) return QStringLiteral("Temporary segments: ") + text.mid(QStringLiteral("Временные сегменты: ").size());
        if (text.startsWith(QStringLiteral("Свободно примерно "))) return QStringLiteral("Approximately ") + text.mid(QStringLiteral("Свободно примерно ").size());
        if (text.startsWith(QStringLiteral("Открыть "))) return QStringLiteral("Open ") + text.mid(QStringLiteral("Открыть ").size());
        if (text.startsWith(QStringLiteral("Захват идёт для монитора "))) return QStringLiteral("Capture is running for monitor ") + text.mid(QStringLiteral("Захват идёт для монитора ").size());
        if (text.startsWith(QStringLiteral("Клип сохранён: "))) return QStringLiteral("Clip saved: ") + text.mid(QStringLiteral("Клип сохранён: ").size());
        if (text.startsWith(QStringLiteral("Доступна новая версия "))) return QStringLiteral("A new version is available ") + text.mid(QStringLiteral("Доступна новая версия ").size());
        if (text.startsWith(QStringLiteral("Лог содержит только технические события и не записывает кадры, звук или содержимое окон: "))) {
            return QStringLiteral("The log contains technical events only and never records frames, audio, or window contents: ") +
                   text.mid(QStringLiteral("Лог содержит только технические события и не записывает кадры, звук или содержимое окон: ").size());
        }
    } else {
        if (text.startsWith(QStringLiteral("Clips: "))) return QStringLiteral("Клипы: ") + text.mid(QStringLiteral("Clips: ").size());
        if (text.startsWith(QStringLiteral("Temporary segments: "))) return QStringLiteral("Временные сегменты: ") + text.mid(QStringLiteral("Temporary segments: ").size());
        if (text.startsWith(QStringLiteral("Approximately "))) return QStringLiteral("Свободно примерно ") + text.mid(QStringLiteral("Approximately ").size());
        if (text.startsWith(QStringLiteral("Open "))) return QStringLiteral("Открыть ") + text.mid(QStringLiteral("Open ").size());
        if (text.startsWith(QStringLiteral("Capture is running for monitor "))) return QStringLiteral("Захват идёт для монитора ") + text.mid(QStringLiteral("Capture is running for monitor ").size());
        if (text.startsWith(QStringLiteral("Clip saved: "))) return QStringLiteral("Клип сохранён: ") + text.mid(QStringLiteral("Clip saved: ").size());
        if (text.startsWith(QStringLiteral("A new version is available "))) return QStringLiteral("Доступна новая версия ") + text.mid(QStringLiteral("A new version is available ").size());
        if (text.startsWith(QStringLiteral("The log contains technical events only and never records frames, audio, or window contents: "))) {
            return QStringLiteral("Лог содержит только технические события и не записывает кадры, звук или содержимое окон: ") +
                   text.mid(QStringLiteral("The log contains technical events only and never records frames, audio, or window contents: ").size());
        }
    }
    if (text.startsWith(QStringLiteral("≈ "))) {
        const QString russianSeparator = QStringLiteral(" на ");
        const QString englishSeparator = QStringLiteral(" for ");
        if (english && text.contains(russianSeparator)) {
            QString result = text;
            result.replace(russianSeparator, englishSeparator);
            result.replace(QStringLiteral("выше лимита, экспорт будет остановлен"),
                          QStringLiteral("above the limit; export will stop"));
            result.replace(QStringLiteral("в пределах лимита"), QStringLiteral("within the limit"));
            return result;
        }
        if (!english && text.contains(englishSeparator)) {
            QString result = text;
            result.replace(englishSeparator, russianSeparator);
            result.replace(QStringLiteral("above the limit; export will stop"),
                          QStringLiteral("выше лимита, экспорт будет остановлен"));
            result.replace(QStringLiteral("within the limit"), QStringLiteral("в пределах лимита"));
            return result;
        }
    }
    return text;
}

void translateUiTree(QWidget* root, const QString& language) {
    if (root == nullptr) {
        return;
    }
    QList<QWidget*> widgets{root};
    widgets.append(root->findChildren<QWidget*>());
    for (QWidget* widget : widgets) {
        if (auto* group = qobject_cast<QGroupBox*>(widget)) {
            group->setTitle(localizedUiText(group->title(), language));
        }
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
            button->setText(localizedUiText(button->text(), language));
        }
        if (auto* label = qobject_cast<QLabel*>(widget)) {
            label->setText(localizedUiText(label->text(), language));
        }
        if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            for (int index = 0; index < combo->count(); ++index) {
                combo->setItemText(index, localizedUiText(combo->itemText(index), language));
            }
        }
        if (auto* spin = qobject_cast<QSpinBox*>(widget)) {
            spin->setSpecialValueText(localizedUiText(spin->specialValueText(), language));
        }
    }
}

QString localizeNotification(const QString& text, const QString& language) {
    return localizedUiText(text, language);
}

bool validateLocalStoragePath(const QString& path, QString* reason) {
    const QStorageInfo storage(path);
    if (!storage.isValid() || !storage.isReady()) {
        if (reason != nullptr) *reason = QStringLiteral("Носитель недоступен или ещё не готов.");
        return false;
    }
    if (storage.isReadOnly()) {
        if (reason != nullptr) *reason = QStringLiteral("Носитель доступен только для чтения.");
        return false;
    }
    if (storage.bytesAvailable() < 64LL * 1024LL * 1024LL) {
        if (reason != nullptr) *reason = QStringLiteral("На носителе должно быть не менее 64 MiB свободного места.");
        return false;
    }
#if defined(Q_OS_WIN)
    const QString root = QDir::toNativeSeparators(storage.rootPath());
    const UINT driveType = GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16()));
    if (driveType != DRIVE_FIXED && driveType != DRIVE_RAMDISK) {
        if (reason != nullptr) *reason = QStringLiteral("Выберите каталог на локальном фиксированном диске; сетевые и съёмные носители не поддерживаются.");
        return false;
    }
#endif
    return true;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), settingsStore_(settingsPath()) {
    setWindowTitle(QStringLiteral("LastFrame"));
    setMinimumSize(900, 620);
    loadSettings();
    buildUi();
    notificationOverlay_ = new NotificationOverlay;
    notificationOverlay_->setCorner(QString::fromStdString(settings_.notifications.corner));
    notificationOverlay_->setOpacity(settings_.notifications.opacity);
    if (settingsRecovered_) {
        showToast(QStringLiteral("settings.json повреждён; создана конфигурация по умолчанию."), true);
    }
    setupTray();
    registerHotkeys();
#if defined(Q_OS_WIN)
    WTSRegisterSessionNotification(reinterpret_cast<HWND>(winId()), NOTIFY_FOR_THIS_SESSION);
#endif
    refreshMonitors();
    applyTheme(settings_.extras.value("theme", std::string("dark")) != "light");

    connect(&recorder_, &Media::PortableSegmentRecorder::started, this, [this] {
        trayError_ = false;
        trayWarning_ = false;
        showToast(QStringLiteral("Буфер запущен"));
        applyRecorderState();
    });
    connect(&recorder_, &Media::PortableSegmentRecorder::stopped, this, [this] {
        trayError_ = false;
        trayWarning_ = false;
        showToast(QStringLiteral("Буфер остановлен"));
        applyRecorderState();
    });
    connect(&recorder_, &Media::PortableSegmentRecorder::pausedChanged, this, [this] {
        applyRecorderState();
    });
    connect(&recorder_, &Media::PortableSegmentRecorder::microphoneMuteChanged, this, [this](const bool muted) {
        if (trayMuteAction_ != nullptr) {
            trayMuteAction_->setText(localizedUiText(muted ? QStringLiteral("Включить микрофон")
                                                          : QStringLiteral("Выключить микрофон"),
                                                      QString::fromStdString(settings_.language)));
        }
        showToast(muted ? QStringLiteral("Микрофон выключен") : QStringLiteral("Микрофон включён"));
    });
    connect(&recorder_, &Media::PortableSegmentRecorder::message, this, &MainWindow::showRecorderMessage);
    connect(&recorder_, &Media::PortableSegmentRecorder::error, this, &MainWindow::showRecorderError);
    connect(&recorder_, &Media::PortableSegmentRecorder::clipSaved, this, &MainWindow::onClipSaved);
    connect(&hotkeys_, &Platform::GlobalHotkeyManager::activated, this, &MainWindow::handleHotkey);
    connect(&hotkeys_, &Platform::GlobalHotkeyManager::registrationError, this, &MainWindow::showRecorderError);
    connect(&updater_, &Platform::UpdateChecker::updateAvailable, this,
            [this](const QString& version, const QUrl& url) {
                latestReleaseUrl_ = url;
                updateButton_->setText(localizedUiText(QStringLiteral("Открыть %1").arg(version),
                                                       QString::fromStdString(settings_.language)));
                showToast(QStringLiteral("Доступна новая версия %1.").arg(version));
            });
    connect(&updater_, &Platform::UpdateChecker::upToDate, this,
            [this] { showToast(QStringLiteral("Установлена актуальная версия.")); });
    connect(&updater_, &Platform::UpdateChecker::noRelease, this,
            [this] { showToast(QStringLiteral("Опубликованных releases пока нет.")); });
    connect(&updater_, &Platform::UpdateChecker::error, this, &MainWindow::showRecorderError);
    connect(&capabilityProbe_, &Platform::FfmpegCapabilityProbe::finished, this,
            [this](const Platform::FfmpegCapabilities& capabilities) {
                capabilities_ = capabilities;
                recorder_.setGpuScalerCapability(capabilities.scaleCuda);
                if (!capabilities.available) {
                    capabilitySummary_ = QStringLiteral("FFmpeg не найден. Положите ffmpeg.exe рядом с LastFrame.exe или добавьте его в PATH.");
                } else {
                    const QString capture = capabilities.desktopDuplication ? QStringLiteral("Desktop Duplication")
                                                                             : capabilities.gdiCapture ? QStringLiteral("GDI fallback")
                                                                                                       : QStringLiteral("capture backend не найден");
                    QStringList encoderList;
                    if (capabilities.nvencH264) {
                        encoderList << QStringLiteral("NVENC H.264");
                    }
                    if (capabilities.amfH264) {
                        encoderList << QStringLiteral("AMD AMF H.264");
                    }
                    if (capabilities.qsvH264) {
                        encoderList << QStringLiteral("Intel QSV H.264");
                    }
                    if (capabilities.videoToolboxH264) {
                        encoderList << QStringLiteral("Apple VideoToolbox H.264");
                    }
                    if (capabilities.softwareH264) {
                        encoderList << QStringLiteral("software H.264");
                    }
                    if (capabilities.vp9) {
                        encoderList << QStringLiteral("VP9");
                    }
                    if (capabilities.scaleCuda) {
                        encoderList << QStringLiteral("CUDA scaler");
                    }
                    QStringList audioList;
                    if (capabilities.wasapi) {
                        audioList << QStringLiteral("WASAPI");
                    }
                    if (capabilities.directShow) {
                        audioList << QStringLiteral("DirectShow mic");
                    }
                    const QString encoders = encoderList.join(QStringLiteral(", "));
                    const QString audio = audioList.join(QStringLiteral(", "));
                    capabilitySummary_ = QStringLiteral("FFmpeg: %1\nCapture: %2\nEncoders: %3\nAudio: %4")
                                             .arg(capabilities.version.isEmpty() ? QStringLiteral("доступен") : capabilities.version,
                                                  capture, encoders.isEmpty() ? QStringLiteral("нет") : encoders,
                                                  audio.isEmpty() ? QStringLiteral("нет") : audio);
                }
                applyRecorderState();
            });
    capabilityProbe_.probe(locateFfmpegForUi());
    monitorTimer_.setInterval(500);
    connect(&monitorTimer_, &QTimer::timeout, this, &MainWindow::refreshMonitors);
    monitorTimer_.start();
    autoResumeTimer_.setSingleShot(true);
    autoResumeTimer_.setInterval(2000);
    connect(&autoResumeTimer_, &QTimer::timeout, this, [this] {
        if (!monitorResumePending_ || autoResumeSignature_ != monitorSignature_ ||
            !settings_.capture.autoResume || recorder_.isRecording()) {
            return;
        }
        const QString selectedId = QString::fromStdString(settings_.capture.monitorId);
        const bool monitorAvailable = selectedId.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0
                                      ? !monitors_.isEmpty()
                                      : Platform::MonitorEnumerator::indexForId(monitors_, selectedId) >= 0;
        if (!monitorAvailable) {
            return;
        }
        monitorResumePending_ = false;
        if (recorder_.isPaused()) {
            recorder_.resume();
        } else {
            recorder_.start(settings_);
        }
        showToast(QStringLiteral("Захват автоматически продолжен после восстановления источника."));
    });
    applyRecorderState();
    QTimer::singleShot(0, this, [this] {
        if (firstRun_) {
            showFirstRunDialog();
        }
    });
}

MainWindow::~MainWindow() {
#if defined(Q_OS_WIN)
    WTSUnRegisterSessionNotification(reinterpret_cast<HWND>(winId()));
#endif
    saveSettings();
    delete notificationOverlay_;
}

void MainWindow::buildUi() {
    auto* root = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* sidebar = new QWidget(root);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(220);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(20, 24, 14, 20);
    sidebarLayout->setSpacing(5);

    auto* branding = new QWidget(sidebar);
    auto* brandingLayout = new QHBoxLayout(branding);
    brandingLayout->setContentsMargins(0, 0, 0, 0);
    brandingLayout->setSpacing(10);
    auto* logoIcon = new QLabel(branding);
    logoIcon->setObjectName(QStringLiteral("logoIcon"));
    logoIcon->setFixedSize(42, 42);
    logoIcon->setAlignment(Qt::AlignCenter);
    const QPixmap logoPixmap(QStringLiteral(":/branding/lastframe-icon.png"));
    if (!logoPixmap.isNull()) {
        logoIcon->setPixmap(logoPixmap.scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    brandingLayout->addWidget(logoIcon);
    auto* logo = new QLabel(QStringLiteral("LastFrame"), branding);
    logo->setObjectName(QStringLiteral("logo"));
    brandingLayout->addWidget(logo);
    brandingLayout->addStretch();
    sidebarLayout->addWidget(branding);
    sidebarLayout->addWidget(description(QStringLiteral("Сохраняйте последние секунды игры"), sidebar));
    sidebarLayout->addSpacing(22);

    pages_ = new QStackedWidget(root);
    const QStringList pageNames{
        QStringLiteral("Overview"), QStringLiteral("Capture"), QStringLiteral("Video"),
        QStringLiteral("Audio"), QStringLiteral("Hotkeys"), QStringLiteral("Storage"),
        QStringLiteral("Notifications"), QStringLiteral("Advanced"), QStringLiteral("About"),
    };
    const QList<std::function<QWidget*()>> builders{
        [this] { return buildOverviewPage(); }, [this] { return buildCapturePage(); },
        [this] { return buildVideoPage(); }, [this] { return buildAudioPage(); },
        [this] { return buildHotkeysPage(); }, [this] { return buildStoragePage(); },
        [this] { return buildNotificationsPage(); }, [this] { return buildAdvancedPage(); },
        [this] { return buildAboutPage(); },
    };
    for (int index = 0; index < pageNames.size(); ++index) {
        auto* button = new QPushButton(pageNames.at(index), sidebar);
        button->setObjectName(QStringLiteral("navButton"));
        button->setCheckable(true);
        connect(button, &QPushButton::clicked, this, [this, index] { selectPage(index); });
        sidebarLayout->addWidget(button);
        pages_->addWidget(builders.at(index)());
    }
    sidebarLayout->addStretch();
    auto* version = new QLabel(QStringLiteral("v%1.%2.%3").arg(LASTFRAME_VERSION_MAJOR)
                                   .arg(LASTFRAME_VERSION_MINOR).arg(LASTFRAME_VERSION_PATCH), sidebar);
    version->setObjectName(QStringLiteral("version"));
    sidebarLayout->addWidget(version);

    rootLayout->addWidget(sidebar);
    rootLayout->addWidget(pages_, 1);
    setCentralWidget(root);
    translateUiTree(root, QString::fromStdString(settings_.language));
    selectPage(0);
}

void MainWindow::rebuildUi() {
    const int currentPage = pages_ == nullptr ? 0 : pages_->currentIndex();
    if (QWidget* oldCentral = takeCentralWidget(); oldCentral != nullptr) {
        delete oldCentral;
    }
    buildUi();
    populateMonitorCombo();
    applyTheme(settings_.extras.value("theme", std::string("dark")) != "light");
    applyRecorderState();
    selectPage(currentPage);
    setupTray();
}

QWidget* MainWindow::buildOverviewPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->setSpacing(20);
    layout->addWidget(heading(QStringLiteral("Overview"), page));
    layout->addWidget(description(QStringLiteral("Локальный кольцевой буфер для быстрого сохранения игровых моментов."), page));

    auto* stateBox = new QGroupBox(QStringLiteral("Состояние буфера"), page);
    auto* stateLayout = new QVBoxLayout(stateBox);
    statusLabel_ = new QLabel(QStringLiteral("Выключен"), stateBox);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    statusDetails_ = new QLabel(QStringLiteral("Нажмите «Начать буфер», чтобы начать захват."), stateBox);
    statusDetails_->setWordWrap(true);
    stateLayout->addWidget(statusLabel_);
    stateLayout->addWidget(statusDetails_);
    layout->addWidget(stateBox);

    auto* controls = new QHBoxLayout;
    startButton_ = new QPushButton(QStringLiteral("Начать буфер"), page);
    pauseButton_ = new QPushButton(QStringLiteral("Пауза"), page);
    saveButton_ = new QPushButton(QStringLiteral("Сохранить клип"), page);
    clearButton_ = new QPushButton(QStringLiteral("Очистить"), page);
    saveButton_->setObjectName(QStringLiteral("primaryButton"));
    controls->addWidget(startButton_);
    controls->addWidget(pauseButton_);
    controls->addWidget(saveButton_);
    controls->addWidget(clearButton_);
    controls->addStretch();
    layout->addLayout(controls);
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startOrStop);
    connect(pauseButton_, &QPushButton::clicked, this, &MainWindow::pauseOrResume);
    connect(saveButton_, &QPushButton::clicked, this, &MainWindow::saveClip);
    connect(clearButton_, &QPushButton::clicked, this, &MainWindow::clearBuffer);

    auto* summary = new QHBoxLayout;
    summary->addWidget(card(QStringLiteral("Монитор"), QStringLiteral("Один выбранный монитор, cursor capture включён по умолчанию."), page));
    summary->addWidget(card(QStringLiteral("Буфер"), QStringLiteral("Последние N секунд; сегменты ограничиваются по времени и очереди."), page));
    summary->addWidget(card(QStringLiteral("Приватность"), QStringLiteral("Кадры, звук и диагностика остаются на этом компьютере."), page));
    layout->addLayout(summary);
    layout->addStretch();
    return page;
}

QWidget* MainWindow::buildCapturePage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->addWidget(heading(QStringLiteral("Capture"), page));
    layout->addWidget(description(QStringLiteral("Выберите источник и безопасные параметры захвата."), page));
    auto* form = new QFormLayout;
    monitorCombo_ = new QComboBox(page);
    form->addRow(QStringLiteral("Монитор"), monitorCombo_);
    sourceCombo_ = new QComboBox(page);
    sourceCombo_->addItem(QStringLiteral("Весь монитор"), QStringLiteral("full_monitor"));
    sourceCombo_->addItem(QStringLiteral("Прямоугольная область"), QStringLiteral("custom_region"));
    sourceCombo_->setCurrentIndex(settings_.capture.source == "custom_region" ? 1 : 0);
    form->addRow(QStringLiteral("Источник"), sourceCombo_);
    regionButton_ = new QPushButton(QStringLiteral("Выбрать область"), page);
    regionButton_->setEnabled(sourceCombo_->currentIndex() == 1);
    if (settings_.capture.regionWidth > 3 && settings_.capture.regionHeight > 3) {
        regionButton_->setText(QStringLiteral("%1 × %2 (%3, %4)")
                                   .arg(settings_.capture.regionWidth)
                                   .arg(settings_.capture.regionHeight)
                                   .arg(settings_.capture.regionX)
                                   .arg(settings_.capture.regionY));
    }
    form->addRow(QStringLiteral("Регион"), regionButton_);
    auto* cursor = new QCheckBox(QStringLiteral("Показывать курсор"), page);
    cursor->setChecked(settings_.capture.showCursor);
    form->addRow(QString(), cursor);
    auto* autoResume = new QCheckBox(QStringLiteral("Автоматически продолжать после изменения монитора"), page);
    autoResume->setChecked(settings_.capture.autoResume);
    form->addRow(QString(), autoResume);
    fpsSpin_ = new QSpinBox(page);
    fpsSpin_->setRange(15, 360);
    fpsSpin_->setValue(settings_.capture.fps);
    form->addRow(QStringLiteral("FPS"), fpsSpin_);
    auto* resolutionPreset = new QComboBox(page);
    resolutionPreset->addItem(QStringLiteral("Native"), QStringLiteral("0x0"));
    resolutionPreset->addItem(QStringLiteral("1280 × 720"), QStringLiteral("1280x720"));
    resolutionPreset->addItem(QStringLiteral("1920 × 1080"), QStringLiteral("1920x1080"));
    resolutionPreset->addItem(QStringLiteral("2560 × 1440"), QStringLiteral("2560x1440"));
    resolutionPreset->addItem(QStringLiteral("3840 × 2160"), QStringLiteral("3840x2160"));
    resolutionPreset->addItem(QStringLiteral("Custom"), QStringLiteral("custom"));
    form->addRow(QStringLiteral("Разрешение вывода"), resolutionPreset);
    outputWidthSpin_ = new QSpinBox(page);
    outputWidthSpin_->setRange(0, 16384);
    outputWidthSpin_->setSpecialValueText(QStringLiteral("Native"));
    outputWidthSpin_->setValue(settings_.capture.outputWidth);
    form->addRow(QStringLiteral("Ширина вывода"), outputWidthSpin_);
    outputHeightSpin_ = new QSpinBox(page);
    outputHeightSpin_->setRange(0, 16384);
    outputHeightSpin_->setSpecialValueText(QStringLiteral("Native"));
    outputHeightSpin_->setValue(settings_.capture.outputHeight);
    form->addRow(QStringLiteral("Высота вывода"), outputHeightSpin_);
    const auto resolutionMatches = [this](const QString& value) {
        const QStringList parts = value.split(QLatin1Char('x'));
        return parts.size() == 2 && settings_.capture.outputWidth == parts.at(0).toInt() &&
               settings_.capture.outputHeight == parts.at(1).toInt();
    };
    int resolutionIndex = resolutionPreset->count() - 1;
    for (int index = 0; index < resolutionPreset->count() - 1; ++index) {
        if (resolutionMatches(resolutionPreset->itemData(index).toString())) {
            resolutionIndex = index;
            break;
        }
    }
    resolutionPreset->setCurrentIndex(resolutionIndex);
    const auto applyResolutionPreset = [this, resolutionPreset](int index) {
        const bool custom = index == resolutionPreset->count() - 1;
        if (!custom) {
            const QStringList parts = resolutionPreset->itemData(index).toString().split(QLatin1Char('x'));
            if (parts.size() == 2) {
                const QSignalBlocker widthBlocker(outputWidthSpin_);
                const QSignalBlocker heightBlocker(outputHeightSpin_);
                const int width = parts.at(0).toInt();
                const int height = parts.at(1).toInt();
                outputWidthSpin_->setValue(width);
                outputHeightSpin_->setValue(height);
                settings_.capture.outputWidth = width;
                settings_.capture.outputHeight = height;
            }
        }
        outputWidthSpin_->setReadOnly(!custom);
        outputHeightSpin_->setReadOnly(!custom);
    };
    applyResolutionPreset(resolutionIndex);
    layout->addLayout(form);
    layout->addWidget(description(QStringLiteral("Регион ограничивается выбранным монитором. HDR-мониторы помечаются как HDR → SDR: защищённый контент не обходится, а HDR-диапазон может быть потерян. Параметры применяются при следующем запуске буфера."), page));
    layout->addStretch();
    connect(monitorCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0 && index < monitors_.size()) {
            settings_.capture.monitorId = monitors_.at(index).id.toStdString();
        }
    });
    connect(sourceCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        settings_.capture.source = sourceCombo_->itemData(index).toString().toStdString();
        regionButton_->setEnabled(index == 1);
    });
    connect(regionButton_, &QPushButton::clicked, this, &MainWindow::chooseRegion);
    connect(cursor, &QCheckBox::toggled, this, [this](bool value) { settings_.capture.showCursor = value; });
    connect(autoResume, &QCheckBox::toggled, this, [this](bool value) {
        settings_.capture.autoResume = value;
        if (!value) {
            monitorResumePending_ = false;
            autoResumeTimer_.stop();
        }
    });
    connect(fpsSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        settings_.capture.fps = value;
        updateCapabilityWarning();
    });
    connect(resolutionPreset, qOverload<int>(&QComboBox::currentIndexChanged), this, applyResolutionPreset);
    connect(outputWidthSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, resolutionPreset](int value) {
                settings_.capture.outputWidth = value;
                if (resolutionPreset->currentIndex() != resolutionPreset->count() - 1) {
                    resolutionPreset->setCurrentIndex(resolutionPreset->count() - 1);
                }
            });
    connect(outputHeightSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, resolutionPreset](int value) {
                settings_.capture.outputHeight = value;
                if (resolutionPreset->currentIndex() != resolutionPreset->count() - 1) {
                    resolutionPreset->setCurrentIndex(resolutionPreset->count() - 1);
                }
            });
    return page;
}

QWidget* MainWindow::buildVideoPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->addWidget(heading(QStringLiteral("Video"), page));
    layout->addWidget(description(QStringLiteral("MVP использует MP4 как основной формат и аппаратный H.264 NVENC на NVIDIA."), page));
    capabilityWarningLabel_ = new QLabel(page);
    capabilityWarningLabel_->setWordWrap(true);
    capabilityWarningLabel_->setObjectName(QStringLiteral("warningLabel"));
    layout->addWidget(capabilityWarningLabel_);
    auto* form = new QFormLayout;
    containerCombo_ = new QComboBox(page);
    containerCombo_->addItem(QStringLiteral("MP4"), QStringLiteral("mp4"));
    containerCombo_->addItem(QStringLiteral("MKV"), QStringLiteral("mkv"));
    containerCombo_->addItem(QStringLiteral("WebM"), QStringLiteral("webm"));
    const int containerIndex = containerCombo_->findData(QString::fromStdString(settings_.video.container));
    containerCombo_->setCurrentIndex(containerIndex >= 0 ? containerIndex : 0);
    form->addRow(QStringLiteral("Контейнер"), containerCombo_);
    codecCombo_ = new QComboBox(page);
    codecCombo_->addItem(QStringLiteral("Auto"), QStringLiteral("auto"));
    codecCombo_->addItem(QStringLiteral("H.264 NVENC"), QStringLiteral("h264_nvenc"));
    codecCombo_->addItem(QStringLiteral("H.264 AMD AMF"), QStringLiteral("h264_amf"));
    codecCombo_->addItem(QStringLiteral("H.264 Intel QSV"), QStringLiteral("h264_qsv"));
    codecCombo_->addItem(QStringLiteral("H.264 Apple VideoToolbox"), QStringLiteral("h264_videotoolbox"));
    codecCombo_->addItem(QStringLiteral("Software H.264"), QStringLiteral("libx264"));
    codecCombo_->addItem(QStringLiteral("VP9 (WebM)"), QStringLiteral("libvpx-vp9"));
    const int codecIndex = codecCombo_->findData(QString::fromStdString(settings_.video.codec));
    codecCombo_->setCurrentIndex(codecIndex >= 0 ? codecIndex : 0);
    form->addRow(QStringLiteral("Кодек"), codecCombo_);
    presetCombo_ = new QComboBox(page);
    presetCombo_->addItems({QStringLiteral("Low"), QStringLiteral("Medium"), QStringLiteral("High"),
                            QStringLiteral("Ultra"), QStringLiteral("Custom")});
    const int presetIndex = presetCombo_->findText(QString::fromStdString(settings_.video.preset), Qt::MatchFixedString);
    presetCombo_->setCurrentIndex(presetIndex >= 0 ? presetIndex : 2);
    form->addRow(QStringLiteral("Пресет"), presetCombo_);
    bitrateSpin_ = new QSpinBox(page);
    bitrateSpin_->setRange(1000, 100000);
    bitrateSpin_->setSuffix(QStringLiteral(" kbps"));
    bitrateSpin_->setValue(settings_.video.customBitrateKbps);
    form->addRow(QStringLiteral("Custom bitrate"), bitrateSpin_);
    maxFileSizeSpin_ = new QSpinBox(page);
    maxFileSizeSpin_->setRange(64, 4096);
    maxFileSizeSpin_->setSuffix(QStringLiteral(" MiB"));
    maxFileSizeSpin_->setValue(settings_.video.maxFileSizeMiB);
    form->addRow(QStringLiteral("Лимит файла"), maxFileSizeSpin_);
    estimatedSizeLabel_ = new QLabel(page);
    estimatedSizeLabel_->setWordWrap(true);
    form->addRow(QStringLiteral("Оценка размера"), estimatedSizeLabel_);
    layout->addLayout(form);
    layout->addWidget(description(QStringLiteral("MP4/MKV используют H.264, WebM — VP9. Auto выбирает доступный hardware H.264 через capability probe и сохраняет fallback через software encoder."), page));
    layout->addStretch();
    connect(containerCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        settings_.video.container = containerCombo_->itemData(index).toString().toStdString();
        const bool webm = settings_.video.container == "webm";
        if (webm && codecCombo_->currentData().toString() != QStringLiteral("libvpx-vp9")) {
            codecCombo_->setCurrentIndex(codecCombo_->findData(QStringLiteral("libvpx-vp9")));
        } else if (!webm && codecCombo_->currentData().toString() == QStringLiteral("libvpx-vp9")) {
            codecCombo_->setCurrentIndex(codecCombo_->findData(QStringLiteral("auto")));
        }
        updateCapabilityWarning();
    });
    connect(codecCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (containerCombo_->currentData().toString() == QStringLiteral("webm") &&
                    codecCombo_->itemData(index).toString() != QStringLiteral("libvpx-vp9")) {
                    codecCombo_->setCurrentIndex(codecCombo_->findData(QStringLiteral("libvpx-vp9")));
                    return;
                }
                settings_.video.codec = codecCombo_->itemData(index).toString().toStdString();
                updateCapabilityWarning();
            });
    connect(presetCombo_, &QComboBox::currentTextChanged, this,
            [this](const QString& value) {
                settings_.video.preset = value.toLower().toStdString();
                const bool custom = value.compare(QStringLiteral("Custom"), Qt::CaseInsensitive) == 0;
                bitrateSpin_->setReadOnly(!custom);
                if (!custom) {
                    const int bitrate = value.compare(QStringLiteral("Low"), Qt::CaseInsensitive) == 0 ? 6000
                                        : value.compare(QStringLiteral("Medium"), Qt::CaseInsensitive) == 0 ? 10000
                                        : value.compare(QStringLiteral("Ultra"), Qt::CaseInsensitive) == 0 ? 24000
                                                                                                           : 16000;
                    bitrateSpin_->setValue(bitrate);
                }
            });
    const auto updateEstimatedSize = [this] {
        const int duration = durationSpin_ == nullptr ? settings_.buffer.durationSeconds : durationSpin_->value();
        const double estimatedMiB = static_cast<double>(settings_.video.customBitrateKbps) * duration / 8.0 / 1024.0;
        const bool overLimit = estimatedMiB > settings_.video.maxFileSizeMiB;
        estimatedSizeLabel_->setText(localizedUiText(QStringLiteral("≈ %1 MiB на %2 s; %3")
                                                          .arg(estimatedMiB, 0, 'f', 1)
                                                          .arg(duration)
                                                          .arg(overLimit ? QStringLiteral("выше лимита, экспорт будет остановлен")
                                                                         : QStringLiteral("в пределах лимита")),
                                                     QString::fromStdString(settings_.language)));
        estimatedSizeLabel_->setStyleSheet(overLimit ? QStringLiteral("color: #D94841;") : QString());
    };
    connect(bitrateSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, updateEstimatedSize](int value) { settings_.video.customBitrateKbps = value; updateEstimatedSize(); });
    connect(maxFileSizeSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, updateEstimatedSize](int value) {
                settings_.video.maxFileSizeMiB = value;
                updateEstimatedSize();
            });
    const bool customPreset = presetCombo_->currentText().compare(QStringLiteral("Custom"), Qt::CaseInsensitive) == 0;
    bitrateSpin_->setReadOnly(!customPreset);
    if (!customPreset) {
        const QString value = presetCombo_->currentText();
        const int bitrate = value.compare(QStringLiteral("Low"), Qt::CaseInsensitive) == 0 ? 6000
                            : value.compare(QStringLiteral("Medium"), Qt::CaseInsensitive) == 0 ? 10000
                            : value.compare(QStringLiteral("Ultra"), Qt::CaseInsensitive) == 0 ? 24000 : 16000;
        bitrateSpin_->setValue(bitrate);
    }
    updateEstimatedSize();
    updateCapabilityWarning();
    return page;
}

QWidget* MainWindow::buildAudioPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->addWidget(heading(QStringLiteral("Audio"), page));
    layout->addWidget(description(QStringLiteral("Системный звук подключается через WASAPI loopback, микрофон — через доступный локальный audio backend. Источники не покидают компьютер."), page));
    auto* form = new QFormLayout;
    systemAudioCheck_ = new QCheckBox(QStringLiteral("Системный звук"), page);
    systemAudioCheck_->setChecked(settings_.audio.systemEnabled);
    microphoneCheck_ = new QCheckBox(QStringLiteral("Микрофон"), page);
    microphoneCheck_->setChecked(settings_.audio.microphoneEnabled);
    form->addRow(QStringLiteral("Системный звук"), systemAudioCheck_);
    form->addRow(QStringLiteral("Микрофон"), microphoneCheck_);
    const QString ffmpegPath = locateFfmpegForUi();
    auto* systemDeviceCombo = new QComboBox(page);
    systemDeviceCombo->addItem(QStringLiteral("Auto"), QStringLiteral("auto"));
    for (const auto& device : Platform::AudioDeviceEnumerator::systemOutputs(ffmpegPath)) {
        systemDeviceCombo->addItem(device.name, device.id);
    }
    const int systemDeviceIndex = systemDeviceCombo->findData(QString::fromStdString(settings_.audio.systemDeviceId));
    systemDeviceCombo->setCurrentIndex(systemDeviceIndex >= 0 ? systemDeviceIndex : 0);
    form->addRow(QStringLiteral("Устройство system"), systemDeviceCombo);
    auto* microphoneDeviceCombo = new QComboBox(page);
    microphoneDeviceCombo->addItem(QStringLiteral("Auto"), QStringLiteral("auto"));
    const auto microphones = Platform::AudioDeviceEnumerator::microphones(ffmpegPath);
    for (const auto& device : microphones) {
        microphoneDeviceCombo->addItem(device.name, device.id);
    }
    const int microphoneDeviceIndex = microphoneDeviceCombo->findData(QString::fromStdString(settings_.audio.microphoneDeviceId));
    microphoneDeviceCombo->setCurrentIndex(microphoneDeviceIndex >= 0 ? microphoneDeviceIndex : 0);
    microphoneDeviceCombo->setEnabled(!microphones.isEmpty());
    microphoneCheck_->setEnabled(!microphones.isEmpty());
    form->addRow(QStringLiteral("Устройство microphone"), microphoneDeviceCombo);
    auto* systemVolume = new QSlider(Qt::Horizontal, page);
    systemVolume->setRange(0, 200);
    systemVolume->setValue(qBound(0, qRound(settings_.audio.systemVolume * 100.0), 200));
    form->addRow(QStringLiteral("Громкость system"), systemVolume);
    auto* microphoneVolume = new QSlider(Qt::Horizontal, page);
    microphoneVolume->setRange(0, 200);
    microphoneVolume->setValue(qBound(0, qRound(settings_.audio.microphoneVolume * 100.0), 200));
    form->addRow(QStringLiteral("Громкость microphone"), microphoneVolume);
    layout->addLayout(form);
    layout->addWidget(description(QStringLiteral("48 kHz, stereo; AAC для MP4 и Opus для WebM. Если WASAPI недоступен, LastFrame продолжит с микрофоном или видео и запишет причину в диагностику."), page));
    layout->addStretch();
    connect(systemAudioCheck_, &QCheckBox::toggled, this, [this](bool value) { settings_.audio.systemEnabled = value; });
    connect(microphoneCheck_, &QCheckBox::toggled, this, [this](bool value) { settings_.audio.microphoneEnabled = value; });
    connect(systemDeviceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, systemDeviceCombo](int index) { settings_.audio.systemDeviceId = systemDeviceCombo->itemData(index).toString().toStdString(); });
    connect(microphoneDeviceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, microphoneDeviceCombo](int index) { settings_.audio.microphoneDeviceId = microphoneDeviceCombo->itemData(index).toString().toStdString(); });
    connect(systemVolume, &QSlider::valueChanged, this, [this](int value) { settings_.audio.systemVolume = value / 100.0; });
    connect(microphoneVolume, &QSlider::valueChanged, this, [this](int value) { settings_.audio.microphoneVolume = value / 100.0; });
    return page;
}

QWidget* MainWindow::buildHotkeysPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->addWidget(heading(QStringLiteral("Hotkeys"), page));
    layout->addWidget(description(QStringLiteral("Глобальные комбинации работают поверх игры и проверяются до регистрации."), page));
    auto* table = new QFormLayout;
    auto* saveEdit = new QLineEdit(QString::fromStdString(settings_.hotkeys.save), page);
    auto* clearEdit = new QLineEdit(QString::fromStdString(settings_.hotkeys.clear), page);
    auto* pauseEdit = new QLineEdit(QString::fromStdString(settings_.hotkeys.pause), page);
    auto* toggleEdit = new QLineEdit(QString::fromStdString(settings_.hotkeys.toggleCapture), page);
    auto* muteEdit = new QLineEdit(QString::fromStdString(settings_.hotkeys.muteMicrophone), page);
    table->addRow(QStringLiteral("Сохранить"), saveEdit);
    table->addRow(QStringLiteral("Очистить"), clearEdit);
    table->addRow(QStringLiteral("Пауза"), pauseEdit);
    table->addRow(QStringLiteral("Старт/стоп"), toggleEdit);
    table->addRow(QStringLiteral("Mute microphone"), muteEdit);
    layout->addLayout(table);
    auto* apply = new QPushButton(QStringLiteral("Применить хоткеи"), page);
    layout->addWidget(apply);
    layout->addWidget(description(QStringLiteral("Формат: Ctrl+Shift+F10. Пустое поле отключает действие; одинаковые комбинации запрещены."), page));
    layout->addStretch();
    connect(apply, &QPushButton::clicked, this, [this, saveEdit, clearEdit, pauseEdit, toggleEdit, muteEdit] {
        const QStringList values{saveEdit->text().trimmed(), clearEdit->text().trimmed(), pauseEdit->text().trimmed(),
                                 toggleEdit->text().trimmed(), muteEdit->text().trimmed()};
        QSet<QString> unique;
        for (const QString& value : values) {
            if (!value.isEmpty() && unique.contains(value.toLower())) {
                showToast(QStringLiteral("Одинаковые хоткеи использовать нельзя."), true);
                return;
            }
            if (!value.isEmpty()) {
                unique.insert(value.toLower());
            }
        }
        const Core::HotkeySettings previous = settings_.hotkeys;
        settings_.hotkeys.save = saveEdit->text().trimmed().toStdString();
        settings_.hotkeys.clear = clearEdit->text().trimmed().toStdString();
        settings_.hotkeys.pause = pauseEdit->text().trimmed().toStdString();
        settings_.hotkeys.toggleCapture = toggleEdit->text().trimmed().toStdString();
        settings_.hotkeys.muteMicrophone = muteEdit->text().trimmed().toStdString();
        if (!hotkeys_.registerHotkeys(QString::fromStdString(settings_.hotkeys.save),
                                      QString::fromStdString(settings_.hotkeys.clear),
                                      QString::fromStdString(settings_.hotkeys.pause),
                                      QString::fromStdString(settings_.hotkeys.toggleCapture),
                                      QString::fromStdString(settings_.hotkeys.muteMicrophone))) {
            settings_.hotkeys = previous;
            showToast(QStringLiteral("Не удалось зарегистрировать хоткеи; настройки откатились."), true);
            return;
        }
        showToast(QStringLiteral("Хоткеи применены."));
    });
    return page;
}

QWidget* MainWindow::buildStoragePage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->addWidget(heading(QStringLiteral("Storage"), page));
    layout->addWidget(description(QStringLiteral("Готовые клипы не удаляются автоматически. Незавершённые временные сегменты находятся в каталоге приложения."), page));
    auto* path = new QLabel(page);
    path->setWordWrap(true);
    layout->addWidget(path);
    auto* storageState = new QLabel(page);
    storageState->setWordWrap(true);
    layout->addWidget(storageState);
    auto* choose = new QPushButton(QStringLiteral("Выбрать каталог клипов"), page);
    layout->addWidget(choose);
    auto* open = new QPushButton(QStringLiteral("Открыть каталог клипов"), page);
    layout->addWidget(open);

    const QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/LastFrame";
    const auto selectedPath = [this, defaultPath] {
        return settings_.storage.clipsDirectory.empty()
                   ? defaultPath
                   : QString::fromStdString(settings_.storage.clipsDirectory.string());
    };
    const auto refreshStorage = [this, path, storageState, selectedPath] {
        const QString language = QString::fromStdString(settings_.language);
        const QString clipsPath = selectedPath();
        path->setText(localizedUiText(QStringLiteral("Клипы: %1").arg(clipsPath), language));
        const QStorageInfo storage(clipsPath);
        const double freeGiB = storage.isValid() && storage.isReady()
                                   ? static_cast<double>(storage.bytesAvailable()) / 1024.0 / 1024.0 / 1024.0
                                   : 0.0;
        const QString state = !storage.isValid() || !storage.isReady()
                                  ? localizedUiText(QStringLiteral("Носитель недоступен или ещё не готов."), language)
                                  : storage.isReadOnly()
                                        ? localizedUiText(QStringLiteral("Носитель доступен только для чтения."), language)
                                        : localizedUiText(QStringLiteral("Свободно примерно %1 GiB.").arg(freeGiB, 0, 'f', 1), language);
        storageState->setText(state);
    };
    refreshStorage();
    const QString temporaryPath = recorder_.temporaryDirectory().isEmpty()
                                      ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/segments"
                                      : recorder_.temporaryDirectory();
    auto* temporary = new QLabel(localizedUiText(QStringLiteral("Временные сегменты: %1").arg(temporaryPath),
                                                 QString::fromStdString(settings_.language)), page);
    temporary->setWordWrap(true);
    layout->addWidget(temporary);
    auto* clearTemporary = new QPushButton(QStringLiteral("Очистить временные сегменты и буфер"), page);
    layout->addWidget(clearTemporary);
    layout->addWidget(description(QStringLiteral("Можно выбрать только локальный фиксированный диск. Сетевые и съёмные носители блокируются; временные сегменты остаются в локальном каталоге приложения."), page));
    layout->addStretch();
    connect(choose, &QPushButton::clicked, this, [this, page, choose, refreshStorage] {
        const QString current = settings_.storage.clipsDirectory.empty()
                                    ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/LastFrame"
                                    : QString::fromStdString(settings_.storage.clipsDirectory.string());
        const QString selected = QFileDialog::getExistingDirectory(page, QStringLiteral("Каталог клипов"), current);
        if (selected.isEmpty()) {
            return;
        }
        QString reason;
        if (!validateLocalStoragePath(selected, &reason)) {
            QMessageBox::warning(page,
                                 localizedUiText(QStringLiteral("Каталог недоступен"), QString::fromStdString(settings_.language)),
                                 localizedUiText(reason, QString::fromStdString(settings_.language)));
            return;
        }
        settings_.storage.clipsDirectory = std::filesystem::path(selected.toStdString());
        refreshStorage();
        choose->setToolTip(selected);
        showToast(QStringLiteral("Каталог клипов изменён."));
    });
    connect(open, &QPushButton::clicked, this, [selectedPath] {
        const QString directory = selectedPath();
        QDir().mkpath(directory);
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });
    connect(clearTemporary, &QPushButton::clicked, this, &MainWindow::clearBuffer);
    return page;
}

QWidget* MainWindow::buildNotificationsPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->addWidget(heading(QStringLiteral("Notifications"), page));
    layout->addWidget(description(QStringLiteral("Настройте локальные уведомления LastFrame. Звуковая обратная связь не используется."), page));
    auto* form = new QFormLayout;
    auto* enabled = new QCheckBox(QStringLiteral("Показывать уведомления"), page);
    enabled->setChecked(settings_.notifications.enabled);
    form->addRow(QStringLiteral("Системные уведомления"), enabled);
    auto* overlay = new QCheckBox(QStringLiteral("Показывать угловой overlay"), page);
    overlay->setChecked(settings_.notifications.overlayEnabled);
    form->addRow(QStringLiteral("Toast overlay"), overlay);
    auto* language = new QComboBox(page);
    language->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language->setCurrentIndex(std::max(0, language->findData(QString::fromStdString(settings_.notifications.language))));
    form->addRow(QStringLiteral("Язык уведомлений"), language);
    auto* corner = new QComboBox(page);
    corner->addItem(QStringLiteral("Сверху справа"), QStringLiteral("top_right"));
    corner->addItem(QStringLiteral("Сверху слева"), QStringLiteral("top_left"));
    corner->addItem(QStringLiteral("Снизу справа"), QStringLiteral("bottom_right"));
    corner->addItem(QStringLiteral("Снизу слева"), QStringLiteral("bottom_left"));
    corner->setCurrentIndex(std::max(0, corner->findData(QString::fromStdString(settings_.notifications.corner))));
    form->addRow(QStringLiteral("Положение overlay"), corner);
    auto* duration = new QSpinBox(page);
    duration->setRange(500, 10000);
    duration->setSingleStep(250);
    duration->setSuffix(QStringLiteral(" ms"));
    duration->setValue(settings_.notifications.durationMs);
    form->addRow(QStringLiteral("Длительность"), duration);
    auto* opacity = new QSpinBox(page);
    opacity->setRange(20, 100);
    opacity->setSuffix(QStringLiteral(" %"));
    opacity->setValue(static_cast<int>(settings_.notifications.opacity * 100.0 + 0.5));
    form->addRow(QStringLiteral("Непрозрачность"), opacity);
    layout->addLayout(form);
    layout->addWidget(description(QStringLiteral("Системные toast-сообщения выводятся через трей. Overlay не перехватывает мышь и не используется как игровой HUD; на Windows запрашивается исключение из поддерживаемого захвата экрана."), page));
    layout->addStretch();
    connect(enabled, &QCheckBox::toggled, this, [this](bool value) { settings_.notifications.enabled = value; });
    connect(overlay, &QCheckBox::toggled, this, [this](bool value) { settings_.notifications.overlayEnabled = value; });
    connect(language, &QComboBox::currentIndexChanged, this, [this, language](int index) {
        settings_.notifications.language = language->itemData(index).toString().toStdString();
    });
    connect(corner, &QComboBox::currentIndexChanged, this, [this, corner](int index) {
        settings_.notifications.corner = corner->itemData(index).toString().toStdString();
        if (notificationOverlay_ != nullptr) {
            notificationOverlay_->setCorner(corner->itemData(index).toString());
        }
    });
    connect(duration, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { settings_.notifications.durationMs = value; });
    connect(opacity, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        settings_.notifications.opacity = static_cast<double>(value) / 100.0;
        if (notificationOverlay_ != nullptr) {
            notificationOverlay_->setOpacity(settings_.notifications.opacity);
        }
    });
    return page;
}

QWidget* MainWindow::buildAdvancedPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->addWidget(heading(QStringLiteral("Advanced"), page));
    auto* form = new QFormLayout;
    durationSpin_ = new QSpinBox(page);
    durationSpin_->setRange(5, 300);
    durationSpin_->setValue(settings_.buffer.durationSeconds);
    durationSpin_->setSuffix(QStringLiteral(" s"));
    form->addRow(QStringLiteral("Длина буфера"), durationSpin_);
    connect(durationSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        settings_.buffer.durationSeconds = value;
        if (estimatedSizeLabel_ != nullptr) {
            const double estimatedMiB = static_cast<double>(settings_.video.customBitrateKbps) * value / 8.0 / 1024.0;
            estimatedSizeLabel_->setText(localizedUiText(QStringLiteral("≈ %1 MiB на %2 s; %3")
                                                              .arg(estimatedMiB, 0, 'f', 1)
                                                              .arg(value)
                                                              .arg(estimatedMiB > settings_.video.maxFileSizeMiB
                                                                       ? QStringLiteral("выше лимита, экспорт будет остановлен")
                                                                       : QStringLiteral("в пределах лимита")),
                                                         QString::fromStdString(settings_.language)));
            estimatedSizeLabel_->setStyleSheet(estimatedMiB > settings_.video.maxFileSizeMiB
                                                    ? QStringLiteral("color: #D94841;") : QString());
        }
    });
    ramLimitSpin_ = new QSpinBox(page);
    ramLimitSpin_->setRange(64, 16384);
    ramLimitSpin_->setSuffix(QStringLiteral(" MiB"));
    ramLimitSpin_->setValue(settings_.buffer.ramLimitMiB);
    form->addRow(QStringLiteral("RAM limit"), ramLimitSpin_);
    themeCombo_ = new QComboBox(page);
    themeCombo_->addItems({QStringLiteral("Тёмная"), QStringLiteral("Светлая")});
    themeCombo_->setCurrentIndex(settings_.extras.value("theme", std::string("dark")) == "light" ? 1 : 0);
    form->addRow(QStringLiteral("Тема"), themeCombo_);
    languageCombo_ = new QComboBox(page);
    languageCombo_->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    languageCombo_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    languageCombo_->setCurrentIndex(settings_.language == "en" ? 1 : 0);
    form->addRow(QStringLiteral("Язык интерфейса"), languageCombo_);
    layout->addLayout(form);
    ffmpegLabel_ = new QLabel(QStringLiteral("FFmpeg: проверка capability…"), page);
    ffmpegLabel_->setWordWrap(true);
    layout->addWidget(ffmpegLabel_);
    updateButton_ = new QPushButton(QStringLiteral("Проверить обновления"), page);
    layout->addWidget(updateButton_);
    diagnosticsButton_ = new QPushButton(QStringLiteral("Скопировать диагностику"), page);
    layout->addWidget(diagnosticsButton_);
    auto* openLog = new QPushButton(QStringLiteral("Открыть локальный лог"), page);
    layout->addWidget(openLog);
    layout->addWidget(description(QStringLiteral("Лог содержит только технические события и не записывает кадры, звук или содержимое окон: %1").arg(Platform::Diagnostics::logPath()), page));
    layout->addWidget(description(QStringLiteral("Телеметрия отключена. Проверка обновлений будет ручной через GitHub Releases и по умолчанию отключена."), page));
    layout->addStretch();
    connect(themeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::chooseTheme);
    connect(languageCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::chooseLanguage);
    connect(ramLimitSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { settings_.buffer.ramLimitMiB = value; });
    connect(updateButton_, &QPushButton::clicked, this, &MainWindow::checkForUpdates);
    connect(diagnosticsButton_, &QPushButton::clicked, this, &MainWindow::copyDiagnostics);
    connect(openLog, &QPushButton::clicked, this, [] {
        const QString path = Platform::Diagnostics::logPath();
        if (!path.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }
    });
    return page;
}

QWidget* MainWindow::buildAboutPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->addWidget(heading(QStringLiteral("About LastFrame"), page));
    layout->addWidget(description(QStringLiteral("Open-source локальный instant replay recorder. Никаких аккаунтов, облака или фоновой телеметрии."), page));
    layout->addWidget(description(QStringLiteral("Лицензия: GPL-3.0-or-later. Portable release публикуется через GitHub Releases без установщика."), page));
    layout->addStretch();
    return page;
}

void MainWindow::addPageButton(const QString& title, const int pageIndex) {
    Q_UNUSED(title)
    Q_UNUSED(pageIndex)
}

void MainWindow::selectPage(const int pageIndex) {
    if (pages_ == nullptr) {
        return;
    }
    pages_->setCurrentIndex(pageIndex);
    const auto buttons = findChildren<QPushButton*>(QStringLiteral("navButton"));
    for (int index = 0; index < buttons.size(); ++index) {
        buttons.at(index)->setChecked(index == pageIndex);
    }
}

void MainWindow::startOrStop() {
    if (recorder_.isRecording() || recorder_.isPaused()) {
        recorder_.stop();
    } else {
        const int monitorIndex = monitorCombo_ == nullptr ? -1 : monitorCombo_->currentIndex();
        if (monitorIndex < 0 || monitorIndex >= monitors_.size()) {
            showToast(QStringLiteral("Нельзя начать буфер: монитор не выбран."), true);
            return;
        }
        const QString selectedMonitorId = QString::fromStdString(settings_.capture.monitorId);
        if (!selectedMonitorId.isEmpty() && selectedMonitorId.compare(QStringLiteral("auto"), Qt::CaseInsensitive) != 0 &&
            Platform::MonitorEnumerator::indexForId(monitors_, selectedMonitorId) < 0) {
            showToast(QStringLiteral("Выбранный монитор недоступен. Выберите доступный монитор заново."), true);
            return;
        }
        settings_.capture.fps = fpsSpin_ == nullptr ? settings_.capture.fps : fpsSpin_->value();
        settings_.buffer.durationSeconds = durationSpin_ == nullptr ? settings_.buffer.durationSeconds : durationSpin_->value();
        const auto& monitor = monitors_.at(monitorIndex);
        if (monitor.refreshRate > 0 && settings_.capture.fps > monitor.refreshRate) {
            showToast(QStringLiteral("FPS %1 выше текущей частоты монитора %2 Hz. Уменьшите значение перед стартом.")
                         .arg(settings_.capture.fps)
                         .arg(monitor.refreshRate),
                     true);
            return;
        }
        if (settings_.capture.source == "custom_region" &&
            (settings_.capture.regionWidth < 4 || settings_.capture.regionHeight < 4)) {
            showToast(QStringLiteral("Выбран custom region, но область не задана или слишком мала."), true);
            return;
        }
        if (settings_.capture.outputWidth > 0 && settings_.capture.outputHeight > 0 &&
            ((settings_.capture.outputWidth % 2) != 0 || (settings_.capture.outputHeight % 2) != 0)) {
            showToast(QStringLiteral("Выходное разрешение должно быть чётным для выбранного YUV420 профиля."), true);
            return;
        }
        recorder_.start(settings_);
    }
}

void MainWindow::pauseOrResume() {
    if (recorder_.isPaused()) {
        recorder_.resume();
    } else if (recorder_.isRecording()) {
        recorder_.pause();
    }
}

void MainWindow::saveClip() {
    recorder_.saveClip();
}

void MainWindow::clearBuffer() {
    recorder_.clearBuffer();
}

void MainWindow::toggleMicrophoneMute() {
    recorder_.setMicrophoneMuted(!recorder_.isMicrophoneMuted());
}

void MainWindow::chooseRegion() {
    const int index = monitorCombo_ == nullptr ? -1 : monitorCombo_->currentIndex();
    if (index < 0 || index >= monitors_.size()) {
        showToast(QStringLiteral("Сначала выберите монитор."), true);
        return;
    }

    RegionSelector selector(monitors_.at(index).geometry, this);
    if (selector.exec() != QDialog::Accepted) {
        return;
    }
    const QRect selection = selector.selectedRegion();
    if (selection.width() < 4 || selection.height() < 4) {
        showToast(QStringLiteral("Регион слишком мал."), true);
        return;
    }
    settings_.capture.source = "custom_region";
    settings_.capture.regionX = selection.x();
    settings_.capture.regionY = selection.y();
    settings_.capture.regionWidth = selection.width();
    settings_.capture.regionHeight = selection.height();
    sourceCombo_->setCurrentIndex(sourceCombo_->findData(QStringLiteral("custom_region")));
    regionButton_->setText(localizedUiText(QStringLiteral("%1 × %2 (%3, %4)")
                                               .arg(selection.width())
                                               .arg(selection.height())
                                               .arg(selection.x())
                                               .arg(selection.y()),
                                           QString::fromStdString(settings_.language)));
}

void MainWindow::checkForUpdates() {
    if (!latestReleaseUrl_.isEmpty()) {
        QDesktopServices::openUrl(latestReleaseUrl_);
        return;
    }
    updateButton_->setEnabled(false);
    updateButton_->setText(localizedUiText(QStringLiteral("Проверка…"), QString::fromStdString(settings_.language)));
    connect(&updater_, &Platform::UpdateChecker::upToDate, this, [this] {
        updateButton_->setEnabled(true);
        updateButton_->setText(localizedUiText(QStringLiteral("Проверить обновления"), QString::fromStdString(settings_.language)));
    }, Qt::SingleShotConnection);
    connect(&updater_, &Platform::UpdateChecker::noRelease, this, [this] {
        updateButton_->setEnabled(true);
        updateButton_->setText(localizedUiText(QStringLiteral("Проверить обновления"), QString::fromStdString(settings_.language)));
    }, Qt::SingleShotConnection);
    connect(&updater_, &Platform::UpdateChecker::error, this, [this] {
        updateButton_->setEnabled(true);
        updateButton_->setText(localizedUiText(QStringLiteral("Проверить обновления"), QString::fromStdString(settings_.language)));
    }, Qt::SingleShotConnection);
    connect(&updater_, &Platform::UpdateChecker::updateAvailable, this, [this] {
        updateButton_->setEnabled(true);
    }, Qt::SingleShotConnection);
    updater_.check();
}

void MainWindow::copyDiagnostics() {
    const QString selectedMonitor = monitorCombo_ == nullptr ? QString() : monitorCombo_->currentData().toString();
    const QString report = Platform::Diagnostics::snapshot(recorder_.ffmpegPath(), monitors_.size(), selectedMonitor,
                                                           recorder_.isRecording(), recorder_.isPaused());
    QApplication::clipboard()->setText(report);
    showToast(QStringLiteral("Диагностика скопирована в буфер обмена."));
}

void MainWindow::refreshMonitors() {
    const auto detected = Platform::MonitorEnumerator::enumerate();
    QString signature;
    for (const auto& monitor : detected) {
        signature += monitor.id + QStringLiteral("|") + QString::number(monitor.resolution.width()) +
                     QStringLiteral("x") + QString::number(monitor.resolution.height()) + QStringLiteral("|") +
                     QString::number(monitor.nativeResolution.width()) + QStringLiteral("x") +
                     QString::number(monitor.nativeResolution.height()) + QStringLiteral("|") +
                     QString::number(monitor.refreshRate) + QStringLiteral("|") + monitor.orientation +
                     QStringLiteral("|") + QString::number(monitor.devicePixelRatio, 'f', 3) + QStringLiteral("|") +
                     (monitor.hdrEnabled ? QStringLiteral("hdr") : QStringLiteral("sdr")) + QStringLiteral(";");
    }
    const bool changed = monitorSignatureInitialized_ && signature != monitorSignature_;
    monitorSignature_ = signature;
    monitorSignatureInitialized_ = true;
    monitors_ = detected;
    if (changed && (recorder_.isRecording() || recorder_.isPaused())) {
        monitorResumePending_ = settings_.capture.autoResume;
        autoResumeTimer_.stop();
        recorder_.stop();
        showToast(QStringLiteral("[monitor_changed] Конфигурация монитора изменилась. Буфер остановлен; ожидаю стабильное восстановление."), true);
    } else if (monitorResumePending_ && settings_.capture.autoResume && !autoResumeTimer_.isActive()) {
        const QString selectedId = QString::fromStdString(settings_.capture.monitorId);
        const bool monitorAvailable = selectedId.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0
                                      ? !detected.isEmpty()
                                      : Platform::MonitorEnumerator::indexForId(detected, selectedId) >= 0;
        if (monitorAvailable) {
            autoResumeSignature_ = signature;
            autoResumeTimer_.start();
        }
    }
    populateMonitorCombo();
}

void MainWindow::chooseTheme(const int index) {
    settings_.extras["theme"] = index == 0 ? "dark" : "light";
    applyTheme(index == 0);
}

void MainWindow::chooseLanguage(const int index) {
    const std::string language = index == 1 ? "en" : "ru";
    if (settings_.language == language) {
        return;
    }
    settings_.language = language;
    saveSettings();
    rebuildUi();
}

void MainWindow::handleHotkey(const Platform::HotkeyAction action) {
    switch (action) {
    case Platform::HotkeyAction::Save: saveClip(); break;
    case Platform::HotkeyAction::Clear: clearBuffer(); break;
    case Platform::HotkeyAction::Pause: pauseOrResume(); break;
    case Platform::HotkeyAction::ToggleCapture: startOrStop(); break;
    case Platform::HotkeyAction::MuteMicrophone: toggleMicrophoneMute(); break;
    }
}

void MainWindow::showRecorderError(const QString& text) {
    Platform::Diagnostics::append(QStringLiteral("ERROR"), text);
    trayError_ = true;
    showToast(text, true);
    const QString language = QString::fromStdString(settings_.language);
    QMessageBox dialog(QMessageBox::Critical, localizedUiText(QStringLiteral("LastFrame — ошибка"), language),
                       localizedUiText(text, language),
                       QMessageBox::NoButton, this);
    dialog.setInformativeText(localizedUiText(QStringLiteral("Проверьте настройки захвата или перезапустите сессию. Старые сохранённые клипы не затрагиваются."), language));
    dialog.setDetailedText(localizedUiText(QStringLiteral("Технический код и подробности записаны в локальный лог:\n%1")
                                                .arg(Platform::Diagnostics::logPath()), language));
    auto* copyButton = dialog.addButton(localizedUiText(QStringLiteral("Скопировать диагностику"), language), QMessageBox::ActionRole);
    auto* restartButton = dialog.addButton(localizedUiText(QStringLiteral("Перезапустить захват"), language), QMessageBox::AcceptRole);
    dialog.addButton(localizedUiText(QStringLiteral("Закрыть"), language), QMessageBox::RejectRole);
    dialog.exec();
    if (dialog.clickedButton() == copyButton) {
        copyDiagnostics();
    } else if (dialog.clickedButton() == restartButton) {
        recorder_.stop();
        recorder_.start(settings_);
    }
}

void MainWindow::showRecorderMessage(const QString& text) {
    Platform::Diagnostics::append(QStringLiteral("INFO"), text);
    if (text.startsWith(QStringLiteral("[audio_device_lost]"))) {
        trayWarning_ = true;
        updateTrayIcon();
    }
    showToast(text);
}

void MainWindow::onClipSaved(const QString& path) {
    lastSavedPath_ = path;
    Platform::Diagnostics::append(QStringLiteral("INFO"), QStringLiteral("Clip export completed."));
    showToast(QStringLiteral("Клип сохранён: %1").arg(QFileInfo(path).fileName()));
    if (tray_ != nullptr && settings_.notifications.enabled) {
        tray_->showMessage(QStringLiteral("LastFrame"),
                           localizeNotification(QStringLiteral("Клип сохранён"),
                                                QString::fromStdString(settings_.notifications.language)),
                           QSystemTrayIcon::Information, settings_.notifications.durationMs);
    }
}

void MainWindow::showFromSingleInstance() {
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::applyTheme(const bool dark) {
    const QString background = dark ? QStringLiteral("#242424") : QStringLiteral("#F4F4F2");
    const QString surface = dark ? QStringLiteral("#303030") : QStringLiteral("#FFFFFF");
    const QString text = dark ? QStringLiteral("#F5F5F5") : QStringLiteral("#202020");
    const QString muted = dark ? QStringLiteral("#A9A9A9") : QStringLiteral("#6B6B6B");
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget { background: %1; color: %2; font-size: 14px; }"
        "#sidebar { background: %3; border-right: 1px solid %4; }"
        "#logo { font-size: 22px; font-weight: 700; color: %2; }"
        "#pageHeading { font-size: 28px; font-weight: 700; }"
        "#description, #version { color: %5; }"
        "QGroupBox { background: %3; border: 1px solid %4; border-radius: 12px; margin-top: 12px; padding-top: 14px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 5px; color: %5; }"
        "QPushButton { background: %3; color: %2; border: 1px solid %4; border-radius: 9px; padding: 9px 14px; }"
        "QPushButton:hover { border-color: %6; }"
        "QPushButton:checked, #primaryButton { background: %6; color: white; border-color: %6; }"
        "#navButton { text-align: left; border: 0; background: transparent; padding: 10px; }"
        "#navButton:hover { background: %4; }"
        "QComboBox, QSpinBox, QLineEdit { background: %3; color: %2; border: 1px solid %4; border-radius: 8px; padding: 7px; }"
        "QCheckBox { spacing: 8px; padding: 7px 0; }"
        "#statusLabel { color: %6; font-size: 20px; font-weight: 700; }"
        "#warningLabel { color: #E0A800; padding: 6px 0; }"
    ).arg(background, text, surface, dark ? QStringLiteral("#444444") : QStringLiteral("#D8D8D4"), muted, QString::fromLatin1(accent)));
}

void MainWindow::updateCapabilityWarning() {
    if (capabilityWarningLabel_ == nullptr) {
        return;
    }
    const bool english = settings_.language == "en";
    QStringList warnings;
    const auto add = [&warnings, english](const char* russian, const char* englishText) {
        warnings << QString::fromUtf8(english ? englishText : russian);
    };
    if (capabilities_.path.isEmpty()) {
        add("Проверка возможностей FFmpeg ещё не завершена.",
            "The FFmpeg capability check has not finished yet.");
    } else if (!capabilities_.available) {
        add("FFmpeg недоступен: старт буфера будет отклонён.",
            "FFmpeg is unavailable: buffer start will be rejected.");
    } else {
        const QString container = QString::fromStdString(settings_.video.container).toLower();
        const QString codec = QString::fromStdString(settings_.video.codec).toLower();
        const bool anyH264 = capabilities_.nvencH264 || capabilities_.amfH264 || capabilities_.qsvH264 ||
                             capabilities_.videoToolboxH264 || capabilities_.softwareH264;
        if (container == QStringLiteral("webm") && !capabilities_.vp9) {
            add("Для WebM в текущем FFmpeg не найден VP9; экспорт будет недоступен.",
                "VP9 was not found in the current FFmpeg; WebM export will be unavailable.");
        } else if (container != QStringLiteral("webm") &&
                   ((codec == QStringLiteral("h264_nvenc") && !capabilities_.nvencH264) ||
                    (codec == QStringLiteral("h264_amf") && !capabilities_.amfH264) ||
                    (codec == QStringLiteral("h264_qsv") && !capabilities_.qsvH264) ||
                    (codec == QStringLiteral("h264_videotoolbox") && !capabilities_.videoToolboxH264) ||
                    (codec == QStringLiteral("libx264") && !capabilities_.softwareH264) ||
                    (codec == QStringLiteral("auto") && !anyH264))) {
            add("Выбранный профиль H.264 не подтверждён capability probe; будет использован fallback или показана ошибка.",
                "The selected H.264 profile was not confirmed by the capability probe; a fallback or an error will be used.");
        }
#if defined(Q_OS_WIN)
        if (settings_.audio.systemEnabled && !capabilities_.wasapi) {
            add("WASAPI loopback не найден; системный звук будет отключён или заменён fallback.",
                "WASAPI loopback was not found; system audio will be disabled or replaced by a fallback.");
        }
#endif
    }
    const int monitorIndex = monitorCombo_ == nullptr ? -1 : monitorCombo_->currentIndex();
    if (monitorIndex >= 0 && monitorIndex < monitors_.size() && monitors_.at(monitorIndex).refreshRate > 0 &&
        settings_.capture.fps > monitors_.at(monitorIndex).refreshRate) {
        warnings << (english
                         ? QStringLiteral("FPS exceeds the selected monitor refresh rate.")
                         : QStringLiteral("FPS выше частоты обновления выбранного монитора."));
    }
    capabilityWarningLabel_->setText(warnings.join(QStringLiteral("\n")));
    capabilityWarningLabel_->setVisible(!warnings.isEmpty());
}

void MainWindow::applyRecorderState() {
    const bool active = recorder_.isRecording();
    const bool paused = recorder_.isPaused();
    const QString language = QString::fromStdString(settings_.language);
    updateCapabilityWarning();
    startButton_->setText(localizedUiText(active || paused ? QStringLiteral("Остановить") : QStringLiteral("Начать буфер"), language));
    pauseButton_->setText(localizedUiText(paused ? QStringLiteral("Продолжить") : QStringLiteral("Пауза"), language));
    pauseButton_->setEnabled(active || paused);
    saveButton_->setEnabled(active || paused);
    statusLabel_->setText(localizedUiText(paused ? QStringLiteral("Пауза") : active ? QStringLiteral("Активен") : QStringLiteral("Выключен"), language));
    statusDetails_->setText(localizedUiText(
        paused ? QStringLiteral("Новые кадры временно не поступают; накопленные сегменты доступны для сохранения.")
               : active ? QStringLiteral("Захват идёт для монитора %1.").arg(monitorCombo_->currentText())
                        : QStringLiteral("Нажмите «Начать буфер», чтобы начать захват."), language));
    updateTrayIcon();
    if (ffmpegLabel_ != nullptr) {
        QString details = capabilitySummary_.isEmpty() ? localizedUiText(QStringLiteral("FFmpeg: поиск capability при старте"), language)
                                                        : localizedUiText(capabilitySummary_, language);
        if (!recorder_.ffmpegPath().isEmpty()) {
            details += QStringLiteral("\n") + localizedUiText(QStringLiteral("Каталог сегментов: %1").arg(recorder_.temporaryDirectory()), language);
        }
        ffmpegLabel_->setText(details);
    }
}

void MainWindow::loadSettings() {
    firstRun_ = !std::filesystem::exists(settingsStore_.path());
    bool recovered = false;
    settings_ = settingsStore_.load(&recovered);
    settingsRecovered_ = recovered;
}

void MainWindow::showFirstRunDialog() {
    const auto autoProfile = [this] {
        settings_.capture.monitorId = "auto";
        settings_.capture.outputWidth = 0;
        settings_.capture.outputHeight = 0;
        settings_.capture.fps = 60;
        if (!monitors_.isEmpty() && monitors_.front().refreshRate > 0) {
            settings_.capture.fps = std::clamp(60, 15, monitors_.front().refreshRate);
        }
        settings_.video.codec = "auto";
        settings_.video.preset = "high";
        settings_.buffer.autoStart = false;
        saveSettings();
        firstRun_ = false;
        applyRecorderState();
    };

    const QString language = QString::fromStdString(settings_.language);
    QMessageBox dialog(QMessageBox::Information, localizedUiText(QStringLiteral("Добро пожаловать в LastFrame"), language),
                       localizedUiText(QStringLiteral("Начальная настройка готова. Профиль Auto использует выбранный монитор, безопасный FPS и аппаратный H.264 с fallback."), language),
                       QMessageBox::NoButton, this);
    dialog.setInformativeText(localizedUiText(QStringLiteral("Захват не запускается автоматически. Можно оставить Auto или открыть страницы настроек для точной конфигурации."), language));
    auto* autoButton = dialog.addButton(localizedUiText(QStringLiteral("Оставить Auto"), language), QMessageBox::AcceptRole);
    auto* settingsButton = dialog.addButton(localizedUiText(QStringLiteral("Открыть настройки"), language), QMessageBox::ActionRole);
    dialog.addButton(localizedUiText(QStringLiteral("Позже"), language), QMessageBox::RejectRole);
    dialog.exec();
    if (dialog.clickedButton() == autoButton) {
        autoProfile();
    } else if (dialog.clickedButton() == settingsButton) {
        autoProfile();
        selectPage(1);
    }
}

void MainWindow::saveSettings() {
    try {
        settingsStore_.save(settings_);
    } catch (const std::exception& error) {
        qWarning("Failed to save settings: %s", error.what());
    }
}

void MainWindow::showToast(const QString& text, const bool isError) {
    if (isError) {
        trayError_ = true;
        updateTrayIcon();
    }
    const QString language = QString::fromStdString(settings_.language);
    statusDetails_->setText(localizedUiText(text, language));
    statusLabel_->setStyleSheet(isError ? QStringLiteral("color: #D94841;") : QStringLiteral("color: #ED760E;"));
    if (!settings_.notifications.enabled || !settings_.notifications.overlayEnabled) {
        return;
    }
    if (notificationOverlay_ == nullptr) {
        return;
    }
    int monitorIndex = Platform::MonitorEnumerator::indexForId(
        monitors_, QString::fromStdString(settings_.capture.monitorId));
    if (monitorIndex < 0 && !monitors_.isEmpty()) {
        monitorIndex = 0;
    }
    if (monitorIndex >= 0 && monitorIndex < monitors_.size()) {
        notificationOverlay_->setMonitorGeometry(monitors_.at(monitorIndex).geometry);
    }
    notificationOverlay_->setCorner(QString::fromStdString(settings_.notifications.corner));
    notificationOverlay_->setOpacity(settings_.notifications.opacity);
    notificationOverlay_->showMessage(
        localizeNotification(text, QString::fromStdString(settings_.notifications.language)), isError,
        settings_.notifications.durationMs);
}

void MainWindow::setupTray() {
    if (tray_ == nullptr) {
        tray_ = new QSystemTrayIcon(this);
        connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger) {
                showFromSingleInstance();
            }
        });
        connect(tray_, &QSystemTrayIcon::messageClicked, this, [this] {
            if (lastSavedPath_.isEmpty()) {
                return;
            }
#if defined(Q_OS_WIN)
            QProcess::startDetached(QStringLiteral("explorer.exe"),
                                    {QStringLiteral("/select,"), QDir::toNativeSeparators(lastSavedPath_)});
#else
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(lastSavedPath_).absolutePath()));
#endif
        });
    }
    if (QMenu* oldMenu = tray_->contextMenu(); oldMenu != nullptr) {
        tray_->setContextMenu(nullptr);
        oldMenu->deleteLater();
    }
    const QString language = QString::fromStdString(settings_.language);
    updateTrayIcon();
    tray_->setToolTip(localizedUiText(QStringLiteral("LastFrame"), language));
    auto* menu = new QMenu(this);
    auto* save = menu->addAction(localizedUiText(QStringLiteral("Сохранить клип"), language));
    auto* toggle = menu->addAction(localizedUiText(QStringLiteral("Начать/остановить буфер"), language));
    auto* pause = menu->addAction(localizedUiText(QStringLiteral("Пауза/продолжение"), language));
    menu->addSeparator();
    auto* clear = menu->addAction(localizedUiText(QStringLiteral("Очистить буфер"), language));
    trayMuteAction_ = menu->addAction(localizedUiText(QStringLiteral("Выключить микрофон"), language));
    auto* openClips = menu->addAction(localizedUiText(QStringLiteral("Открыть папку клипов"), language));
    auto* settings = menu->addAction(localizedUiText(QStringLiteral("Настройки"), language));
    auto* quit = menu->addAction(localizedUiText(QStringLiteral("Выход"), language));
    connect(save, &QAction::triggered, this, &MainWindow::saveClip);
    connect(toggle, &QAction::triggered, this, &MainWindow::startOrStop);
    connect(pause, &QAction::triggered, this, &MainWindow::pauseOrResume);
    connect(clear, &QAction::triggered, this, &MainWindow::clearBuffer);
    connect(trayMuteAction_, &QAction::triggered, this, &MainWindow::toggleMicrophoneMute);
    connect(openClips, &QAction::triggered, this, [this] {
        const QString directory = settings_.storage.clipsDirectory.empty()
                                      ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/LastFrame"
                                      : QString::fromStdString(settings_.storage.clipsDirectory.string());
        QDir().mkpath(directory);
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });
    connect(settings, &QAction::triggered, this, &MainWindow::showFromSingleInstance);
    connect(quit, &QAction::triggered, this, [this] { forceQuit_ = true; qApp->quit(); });
    tray_->setContextMenu(menu);
    tray_->show();
}

void MainWindow::updateTrayIcon() {
    if (tray_ == nullptr) {
        return;
    }
    const QColor color = trayError_ ? QColor(QStringLiteral("#D94841"))
                                    : trayWarning_ || recorder_.isPaused() ? QColor(QStringLiteral("#E0A800"))
                                    : recorder_.isRecording() ? QColor(QStringLiteral("#36B37E"))
                                                              : QColor(QStringLiteral("#8A8A8A"));
    QPixmap pixmap = QIcon(QStringLiteral(":/branding/lastframe-icon.png"))
                         .pixmap(QSize(32, 32), QIcon::Normal, QIcon::On);
    if (pixmap.isNull()) {
        pixmap = QPixmap(32, 32);
        pixmap.fill(Qt::transparent);
    }
    QPainter painter(&pixmap);
    painter.setBrush(color);
    painter.setPen(QPen(Qt::white, 1));
    painter.drawEllipse(21, 21, 10, 10);
    tray_->setIcon(QIcon(pixmap));
}

void MainWindow::registerHotkeys() {
    (void)hotkeys_.registerHotkeys(QString::fromStdString(settings_.hotkeys.save),
                                    QString::fromStdString(settings_.hotkeys.clear),
                                    QString::fromStdString(settings_.hotkeys.pause),
                                    QString::fromStdString(settings_.hotkeys.toggleCapture),
                                    QString::fromStdString(settings_.hotkeys.muteMicrophone));
}

void MainWindow::populateMonitorCombo() {
    if (monitorCombo_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(monitorCombo_);
    monitorCombo_->clear();
    for (const auto& monitor : monitors_) {
        monitorCombo_->addItem(QStringLiteral("%1 — %2x%3 @ %4 Hz%5")
                                   .arg(monitor.name)
                                   .arg(monitor.resolution.width())
                                   .arg(monitor.resolution.height())
                                   .arg(monitor.refreshRate)
                                   .arg(monitor.hdrEnabled ? QStringLiteral(" — HDR → SDR") : QString()),
                               monitor.id);
    }
    const int selected = Platform::MonitorEnumerator::indexForId(
        monitors_, QString::fromStdString(settings_.capture.monitorId));
    if (selected >= 0) {
        monitorCombo_->setCurrentIndex(selected);
    } else if (!monitors_.isEmpty()) {
        monitorCombo_->setCurrentIndex(0);
    }
    const QString storedId = QString::fromStdString(settings_.capture.monitorId);
    const bool automatic = storedId.isEmpty() || storedId.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
    if (!automatic && !monitors_.isEmpty()) {
        const int effectiveIndex = selected >= 0 ? selected : 0;
        const QString canonicalId = monitors_.at(effectiveIndex).id;
        if (storedId != canonicalId) {
            settings_.capture.monitorId = canonicalId.toStdString();
            saveSettings();
        }
    }
    updateCapabilityWarning();
}

std::filesystem::path MainWindow::settingsPath() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return std::filesystem::path((directory + "/settings.json").toStdString());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!forceQuit_ && tray_ != nullptr && tray_->isVisible()) {
        hide();
        event->ignore();
        showToast(QStringLiteral("LastFrame свёрнут в трей."));
        return;
    }
    saveSettings();
    event->accept();
}

#if defined(Q_OS_WIN)
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(eventType)
    auto* msg = static_cast<MSG*>(message);
    if (msg == nullptr) {
        return QMainWindow::nativeEvent(eventType, message, result);
    }
    const auto suspend = [this](const QString& reason) {
        if (lifecycleSuspended_) {
            return;
        }
        lifecycleWasRecording_ = recorder_.isRecording();
        lifecycleSuspended_ = true;
        if (lifecycleWasRecording_) {
            recorder_.pause();
            showToast(reason);
        }
    };
    const auto resume = [this] {
        if (!lifecycleSuspended_) {
            return;
        }
        lifecycleSuspended_ = false;
        if (!lifecycleWasRecording_) {
            return;
        }
        lifecycleWasRecording_ = false;
        if (!settings_.capture.autoResume) {
            showToast(QStringLiteral("Источник вернулся; возобновление отключено в настройках."), true);
            return;
        }
        monitorResumePending_ = true;
        autoResumeSignature_ = monitorSignature_;
        autoResumeTimer_.start(2000);
        showToast(QStringLiteral("Источник вернулся; проверяю его стабильность перед продолжением."));
    };
    if (msg->message == WM_POWERBROADCAST) {
        if (msg->wParam == PBT_APMSUSPEND) {
            suspend(QStringLiteral("Захват приостановлен: Windows переводит систему в sleep."));
            if (result != nullptr) {
                *result = TRUE;
            }
            return true;
        }
        if (msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND) {
            resume();
            if (result != nullptr) {
                *result = TRUE;
            }
            return true;
        }
    } else if (msg->message == WM_WTSSESSION_CHANGE) {
        if (msg->wParam == WTS_SESSION_LOCK) {
            suspend(QStringLiteral("Захват приостановлен: рабочая сессия заблокирована."));
        } else if (msg->wParam == WTS_SESSION_UNLOCK) {
            resume();
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

} // namespace LastFrame::UI
