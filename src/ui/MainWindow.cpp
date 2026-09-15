#include "ui/MainWindow.h"
#include "ui/NotificationOverlay.h"
#include "ui/RegionSelector.h"
#include "platform/AudioDeviceEnumerator.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDesktopServices>
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

QString localizeNotification(const QString& text, const QString& language) {
    if (language.compare(QStringLiteral("en"), Qt::CaseInsensitive) != 0) {
        return text;
    }
    if (text == QStringLiteral("Буфер запущен")) return QStringLiteral("Buffer started");
    if (text == QStringLiteral("Буфер остановлен")) return QStringLiteral("Buffer stopped");
    if (text == QStringLiteral("Микрофон выключен")) return QStringLiteral("Microphone muted");
    if (text == QStringLiteral("Микрофон включён")) return QStringLiteral("Microphone unmuted");
    if (text == QStringLiteral("Установлена актуальная версия.")) return QStringLiteral("You are up to date.");
    if (text == QStringLiteral("Опубликованных releases пока нет.")) return QStringLiteral("No releases have been published yet.");
    if (text == QStringLiteral("Диагностика скопирована в буфер обмена.")) return QStringLiteral("Diagnostics copied to clipboard.");
    if (text == QStringLiteral("Буфер очищен; сохранённые клипы не затронуты.")) return QStringLiteral("Buffer cleared; saved clips were not touched.");
    if (text == QStringLiteral("Клип сохранён")) return QStringLiteral("Clip saved");
    if (text == QStringLiteral("LastFrame свёрнут в трей.")) return QStringLiteral("LastFrame was minimized to the tray.");
    if (text.startsWith(QStringLiteral("Клип сохранён: "))) {
        return QStringLiteral("Clip saved: ") + text.mid(QStringLiteral("Клип сохранён: ").size());
    }
    if (text.startsWith(QStringLiteral("Доступна новая версия "))) {
        return QStringLiteral("A new version is available ") + text.mid(QStringLiteral("Доступна новая версия ").size());
    }
    return text;
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
            trayMuteAction_->setText(muted ? QStringLiteral("Включить микрофон")
                                           : QStringLiteral("Выключить микрофон"));
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
                updateButton_->setText(QStringLiteral("Открыть %1").arg(version));
                showToast(QStringLiteral("Доступна новая версия %1.").arg(version));
            });
    connect(&updater_, &Platform::UpdateChecker::upToDate, this,
            [this] { showToast(QStringLiteral("Установлена актуальная версия.")); });
    connect(&updater_, &Platform::UpdateChecker::noRelease, this,
            [this] { showToast(QStringLiteral("Опубликованных releases пока нет.")); });
    connect(&updater_, &Platform::UpdateChecker::error, this, &MainWindow::showRecorderError);
    connect(&capabilityProbe_, &Platform::FfmpegCapabilityProbe::finished, this,
            [this](const Platform::FfmpegCapabilities& capabilities) {
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
                                      : std::any_of(monitors_.cbegin(), monitors_.cend(),
                                                    [&selectedId](const Platform::MonitorInfo& monitor) {
                                                        return monitor.id == selectedId;
                                                    });
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

    auto* logo = new QLabel(QStringLiteral("LastFrame"), sidebar);
    logo->setObjectName(QStringLiteral("logo"));
    sidebarLayout->addWidget(logo);
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
    selectPage(0);
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
    connect(fpsSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) { settings_.capture.fps = value; });
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
    });
    connect(codecCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (containerCombo_->currentData().toString() == QStringLiteral("webm") &&
                    codecCombo_->itemData(index).toString() != QStringLiteral("libvpx-vp9")) {
                    codecCombo_->setCurrentIndex(codecCombo_->findData(QStringLiteral("libvpx-vp9")));
                    return;
                }
                settings_.video.codec = codecCombo_->itemData(index).toString().toStdString();
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
        estimatedSizeLabel_->setText(QStringLiteral("≈ %1 MiB на %2 s; %3")
                                         .arg(estimatedMiB, 0, 'f', 1)
                                         .arg(duration)
                                         .arg(overLimit ? QStringLiteral("выше лимита, экспорт будет остановлен")
                                                        : QStringLiteral("в пределах лимита")));
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
        const QString clipsPath = selectedPath();
        path->setText(QStringLiteral("Клипы: %1").arg(clipsPath));
        const QStorageInfo storage(clipsPath);
        const double freeGiB = storage.isValid() && storage.isReady()
                                   ? static_cast<double>(storage.bytesAvailable()) / 1024.0 / 1024.0 / 1024.0
                                   : 0.0;
        const QString state = !storage.isValid() || !storage.isReady()
                                  ? QStringLiteral("Носитель недоступен или ещё не готов.")
                                  : storage.isReadOnly()
                                        ? QStringLiteral("Носитель доступен только для чтения.")
                                        : QStringLiteral("Свободно примерно %1 GiB.").arg(freeGiB, 0, 'f', 1);
        storageState->setText(state);
    };
    refreshStorage();
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
            QMessageBox::warning(page, QStringLiteral("Каталог недоступен"), reason);
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
            estimatedSizeLabel_->setText(QStringLiteral("≈ %1 MiB на %2 s; %3")
                                             .arg(estimatedMiB, 0, 'f', 1)
                                             .arg(value)
                                             .arg(estimatedMiB > settings_.video.maxFileSizeMiB
                                                      ? QStringLiteral("выше лимита, экспорт будет остановлен")
                                                      : QStringLiteral("в пределах лимита")));
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
    layout->addLayout(form);
    ffmpegLabel_ = new QLabel(QStringLiteral("FFmpeg: проверка capability…"), page);
    ffmpegLabel_->setWordWrap(true);
    layout->addWidget(ffmpegLabel_);
    updateButton_ = new QPushButton(QStringLiteral("Проверить обновления"), page);
    layout->addWidget(updateButton_);
    diagnosticsButton_ = new QPushButton(QStringLiteral("Скопировать диагностику"), page);
    layout->addWidget(diagnosticsButton_);
    layout->addWidget(description(QStringLiteral("Телеметрия отключена. Проверка обновлений будет ручной через GitHub Releases и по умолчанию отключена."), page));
    layout->addStretch();
    connect(themeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::chooseTheme);
    connect(ramLimitSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { settings_.buffer.ramLimitMiB = value; });
    connect(updateButton_, &QPushButton::clicked, this, &MainWindow::checkForUpdates);
    connect(diagnosticsButton_, &QPushButton::clicked, this, &MainWindow::copyDiagnostics);
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
            !std::any_of(monitors_.cbegin(), monitors_.cend(), [&selectedMonitorId](const Platform::MonitorInfo& monitor) {
                return monitor.id == selectedMonitorId;
            })) {
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
    regionButton_->setText(QStringLiteral("%1 × %2 (%3, %4)")
                               .arg(selection.width())
                               .arg(selection.height())
                               .arg(selection.x())
                               .arg(selection.y()));
}

void MainWindow::checkForUpdates() {
    if (!latestReleaseUrl_.isEmpty()) {
        QDesktopServices::openUrl(latestReleaseUrl_);
        return;
    }
    updateButton_->setEnabled(false);
    updateButton_->setText(QStringLiteral("Проверка…"));
    connect(&updater_, &Platform::UpdateChecker::upToDate, this, [this] {
        updateButton_->setEnabled(true);
        updateButton_->setText(QStringLiteral("Проверить обновления"));
    }, Qt::SingleShotConnection);
    connect(&updater_, &Platform::UpdateChecker::noRelease, this, [this] {
        updateButton_->setEnabled(true);
        updateButton_->setText(QStringLiteral("Проверить обновления"));
    }, Qt::SingleShotConnection);
    connect(&updater_, &Platform::UpdateChecker::error, this, [this] {
        updateButton_->setEnabled(true);
        updateButton_->setText(QStringLiteral("Проверить обновления"));
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
                                      : std::any_of(detected.cbegin(), detected.cend(),
                                                    [&selectedId](const Platform::MonitorInfo& monitor) {
                                                        return monitor.id == selectedId;
                                                    });
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
    QMessageBox dialog(QMessageBox::Critical, QStringLiteral("LastFrame — ошибка"), text,
                       QMessageBox::NoButton, this);
    dialog.setInformativeText(QStringLiteral("Проверьте настройки захвата или перезапустите сессию. Старые сохранённые клипы не затрагиваются."));
    dialog.setDetailedText(QStringLiteral("Технический код и подробности записаны в локальный лог:\n%1")
                               .arg(Platform::Diagnostics::logPath()));
    auto* copyButton = dialog.addButton(QStringLiteral("Скопировать диагностику"), QMessageBox::ActionRole);
    auto* restartButton = dialog.addButton(QStringLiteral("Перезапустить захват"), QMessageBox::AcceptRole);
    dialog.addButton(QStringLiteral("Закрыть"), QMessageBox::RejectRole);
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
    ).arg(background, text, surface, dark ? QStringLiteral("#444444") : QStringLiteral("#D8D8D4"), muted, QString::fromLatin1(accent)));
}

void MainWindow::applyRecorderState() {
    const bool active = recorder_.isRecording();
    const bool paused = recorder_.isPaused();
    startButton_->setText(active || paused ? QStringLiteral("Остановить") : QStringLiteral("Начать буфер"));
    pauseButton_->setText(paused ? QStringLiteral("Продолжить") : QStringLiteral("Пауза"));
    pauseButton_->setEnabled(active || paused);
    saveButton_->setEnabled(active || paused);
    statusLabel_->setText(paused ? QStringLiteral("Пауза") : active ? QStringLiteral("Активен") : QStringLiteral("Выключен"));
    statusDetails_->setText(paused ? QStringLiteral("Новые кадры временно не поступают; накопленные сегменты доступны для сохранения.")
                             : active ? QStringLiteral("Захват идёт для монитора %1.").arg(monitorCombo_->currentText())
                                     : QStringLiteral("Нажмите «Начать буфер», чтобы начать захват."));
    updateTrayIcon();
    if (ffmpegLabel_ != nullptr) {
        QString details = capabilitySummary_.isEmpty() ? QStringLiteral("FFmpeg: поиск capability при старте") : capabilitySummary_;
        if (!recorder_.ffmpegPath().isEmpty()) {
            details += QStringLiteral("\nКаталог сегментов: %1").arg(recorder_.temporaryDirectory());
        }
        ffmpegLabel_->setText(details);
    }
}

void MainWindow::loadSettings() {
    bool recovered = false;
    settings_ = settingsStore_.load(&recovered);
    settingsRecovered_ = recovered;
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
    statusDetails_->setText(text);
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
    tray_ = new QSystemTrayIcon(this);
    updateTrayIcon();
    tray_->setToolTip(QStringLiteral("LastFrame"));
    auto* menu = new QMenu(this);
    auto* save = menu->addAction(QStringLiteral("Сохранить клип"));
    auto* toggle = menu->addAction(QStringLiteral("Начать/остановить буфер"));
    auto* pause = menu->addAction(QStringLiteral("Пауза/продолжение"));
    menu->addSeparator();
    auto* clear = menu->addAction(QStringLiteral("Очистить буфер"));
    trayMuteAction_ = menu->addAction(QStringLiteral("Выключить микрофон"));
    auto* openClips = menu->addAction(QStringLiteral("Открыть папку клипов"));
    auto* settings = menu->addAction(QStringLiteral("Настройки"));
    auto* quit = menu->addAction(QStringLiteral("Выход"));
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
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(3, 3, 26, 26, 7, 7);
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
