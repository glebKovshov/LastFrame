#include "ui/MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QVBoxLayout>
#include <QWidget>

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

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), settingsStore_(settingsPath()) {
    setWindowTitle(QStringLiteral("LastFrame"));
    setMinimumSize(900, 620);
    loadSettings();
    buildUi();
    if (settingsRecovered_) {
        showToast(QStringLiteral("settings.json повреждён; создана конфигурация по умолчанию."), true);
    }
    setupTray();
    registerHotkeys();
    refreshMonitors();
    applyTheme(true);

    connect(&recorder_, &Media::PortableSegmentRecorder::started, this, [this] {
        showToast(QStringLiteral("Буфер запущен"));
        applyRecorderState();
    });
    connect(&recorder_, &Media::PortableSegmentRecorder::stopped, this, [this] {
        showToast(QStringLiteral("Буфер остановлен"));
        applyRecorderState();
    });
    connect(&recorder_, &Media::PortableSegmentRecorder::pausedChanged, this, [this] {
        applyRecorderState();
    });
    connect(&recorder_, &Media::PortableSegmentRecorder::message, this, &MainWindow::showRecorderMessage);
    connect(&recorder_, &Media::PortableSegmentRecorder::error, this, &MainWindow::showRecorderError);
    connect(&recorder_, &Media::PortableSegmentRecorder::clipSaved, this, &MainWindow::onClipSaved);
    connect(&hotkeys_, &Platform::GlobalHotkeyManager::activated, this, &MainWindow::handleHotkey);
    connect(&hotkeys_, &Platform::GlobalHotkeyManager::registrationError, this, &MainWindow::showRecorderError);
    applyRecorderState();
}

MainWindow::~MainWindow() {
    saveSettings();
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
        QStringLiteral("Advanced"), QStringLiteral("About"),
    };
    const QList<std::function<QWidget*()>> builders{
        [this] { return buildOverviewPage(); }, [this] { return buildCapturePage(); },
        [this] { return buildVideoPage(); }, [this] { return buildAudioPage(); },
        [this] { return buildHotkeysPage(); }, [this] { return buildStoragePage(); },
        [this] { return buildAdvancedPage(); }, [this] { return buildAboutPage(); },
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
    auto* source = new QComboBox(page);
    source->addItem(QStringLiteral("Весь монитор"), QStringLiteral("full_monitor"));
    source->addItem(QStringLiteral("Прямоугольная область"), QStringLiteral("custom_region"));
    source->setEnabled(false);
    form->addRow(QStringLiteral("Источник"), source);
    auto* cursor = new QCheckBox(QStringLiteral("Показывать курсор"), page);
    cursor->setChecked(settings_.capture.showCursor);
    form->addRow(QString(), cursor);
    fpsSpin_ = new QSpinBox(page);
    fpsSpin_->setRange(15, 360);
    fpsSpin_->setValue(settings_.capture.fps);
    form->addRow(QStringLiteral("FPS"), fpsSpin_);
    layout->addLayout(form);
    layout->addWidget(description(QStringLiteral("Выбор региона и DPI-aware overlay подключаются следующим Windows-срезом. Пока MVP захватывает весь выбранный монитор."), page));
    layout->addStretch();
    connect(monitorCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0 && index < monitors_.size()) {
            settings_.capture.monitorId = monitors_.at(index).id.toStdString();
        }
    });
    connect(cursor, &QCheckBox::toggled, this, [this](bool value) { settings_.capture.showCursor = value; });
    connect(fpsSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) { settings_.capture.fps = value; });
    return page;
}

QWidget* MainWindow::buildVideoPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->addWidget(heading(QStringLiteral("Video"), page));
    layout->addWidget(description(QStringLiteral("MVP использует MP4 как основной формат и аппаратный H.264 NVENC на NVIDIA."), page));
    auto* form = new QFormLayout;
    auto* container = new QComboBox(page);
    container->addItems({QStringLiteral("MP4"), QStringLiteral("MKV"), QStringLiteral("WebM")});
    container->setCurrentText(QStringLiteral("MP4"));
    container->setEnabled(false);
    form->addRow(QStringLiteral("Контейнер"), container);
    auto* codec = new QComboBox(page);
    codec->addItems({QStringLiteral("Auto"), QStringLiteral("H.264 NVENC"), QStringLiteral("Software fallback")});
    form->addRow(QStringLiteral("Кодек"), codec);
    auto* preset = new QComboBox(page);
    preset->addItems({QStringLiteral("Low"), QStringLiteral("Medium"), QStringLiteral("High"), QStringLiteral("Ultra"), QStringLiteral("Custom")});
    preset->setCurrentText(QStringLiteral("High"));
    form->addRow(QStringLiteral("Пресет"), preset);
    layout->addLayout(form);
    layout->addWidget(description(QStringLiteral("FFmpeg запускается из portable-каталога. При недоступности NVENC приложение сообщает об ошибке; безопасный software fallback добавляется отдельным срезом."), page));
    layout->addStretch();
    return page;
}

QWidget* MainWindow::buildAudioPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->addWidget(heading(QStringLiteral("Audio"), page));
    layout->addWidget(description(QStringLiteral("MVP пытается подключить системный звук через WASAPI loopback. Микрофон и mixer controls сохраняются в конфигурации для следующего audio-среза."), page));
    systemAudioCheck_ = new QCheckBox(QStringLiteral("Системный звук"), page);
    systemAudioCheck_->setChecked(settings_.audio.systemEnabled);
    microphoneCheck_ = new QCheckBox(QStringLiteral("Микрофон"), page);
    microphoneCheck_->setChecked(settings_.audio.microphoneEnabled);
    microphoneCheck_->setEnabled(false);
    layout->addWidget(systemAudioCheck_);
    layout->addWidget(microphoneCheck_);
    layout->addWidget(description(QStringLiteral("Sample rate: 48 kHz, stereo, AAC для MP4. При недоступном loopback будет выполнен video-only fallback с уведомлением."), page));
    layout->addStretch();
    connect(systemAudioCheck_, &QCheckBox::toggled, this, [this](bool value) { settings_.audio.systemEnabled = value; });
    return page;
}

QWidget* MainWindow::buildHotkeysPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->addWidget(heading(QStringLiteral("Hotkeys"), page));
    layout->addWidget(description(QStringLiteral("Глобальные комбинации работают поверх игры и проверяются до регистрации."), page));
    auto* table = new QFormLayout;
    table->addRow(QStringLiteral("Сохранить"), new QLabel(QString::fromStdString(settings_.hotkeys.save), page));
    table->addRow(QStringLiteral("Очистить"), new QLabel(QString::fromStdString(settings_.hotkeys.clear), page));
    table->addRow(QStringLiteral("Пауза"), new QLabel(QString::fromStdString(settings_.hotkeys.pause), page));
    table->addRow(QStringLiteral("Старт/стоп"), new QLabel(QString::fromStdString(settings_.hotkeys.toggleCapture), page));
    table->addRow(QStringLiteral("Mute microphone"), new QLabel(settings_.hotkeys.muteMicrophone.empty() ? QStringLiteral("Не назначен") : QString::fromStdString(settings_.hotkeys.muteMicrophone), page));
    layout->addLayout(table);
    layout->addWidget(description(QStringLiteral("Редактор комбинаций будет добавлен после завершения native hotkey conflict UI."), page));
    layout->addStretch();
    return page;
}

QWidget* MainWindow::buildStoragePage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->addWidget(heading(QStringLiteral("Storage"), page));
    layout->addWidget(description(QStringLiteral("Готовые клипы не удаляются автоматически. Незавершённые временные сегменты находятся в каталоге приложения."), page));
    auto* path = new QLabel(settings_.storage.clipsDirectory.empty()
                                ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/LastFrame"
                                : QString::fromStdString(settings_.storage.clipsDirectory.string()), page);
    path->setWordWrap(true);
    layout->addWidget(path);
    layout->addWidget(description(QStringLiteral("Portable MVP использует локальный системный диск; сетевые и съёмные каталоги не проверяются как гарантированные."), page));
    layout->addStretch();
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
    themeCombo_ = new QComboBox(page);
    themeCombo_->addItems({QStringLiteral("Тёмная"), QStringLiteral("Светлая")});
    form->addRow(QStringLiteral("Тема"), themeCombo_);
    layout->addLayout(form);
    ffmpegLabel_ = new QLabel(QStringLiteral("FFmpeg: поиск при старте буфера"), page);
    ffmpegLabel_->setWordWrap(true);
    layout->addWidget(ffmpegLabel_);
    layout->addWidget(description(QStringLiteral("Телеметрия отключена. Проверка обновлений будет ручной через GitHub Releases и по умолчанию отключена."), page));
    layout->addStretch();
    connect(durationSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) { settings_.buffer.durationSeconds = value; });
    connect(themeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::chooseTheme);
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
        settings_.capture.fps = fpsSpin_ == nullptr ? settings_.capture.fps : fpsSpin_->value();
        settings_.buffer.durationSeconds = durationSpin_ == nullptr ? settings_.buffer.durationSeconds : durationSpin_->value();
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

void MainWindow::refreshMonitors() {
    populateMonitorCombo();
}

void MainWindow::chooseTheme(const int index) {
    applyTheme(index == 0);
}

void MainWindow::handleHotkey(const Platform::HotkeyAction action) {
    switch (action) {
    case Platform::HotkeyAction::Save: saveClip(); break;
    case Platform::HotkeyAction::Clear: clearBuffer(); break;
    case Platform::HotkeyAction::Pause: pauseOrResume(); break;
    case Platform::HotkeyAction::ToggleCapture: startOrStop(); break;
    case Platform::HotkeyAction::MuteMicrophone:
        showToast(QStringLiteral("Mute microphone подключится вместе с audio mixer."));
        break;
    }
}

void MainWindow::showRecorderError(const QString& text) {
    showToast(text, true);
}

void MainWindow::showRecorderMessage(const QString& text) {
    showToast(text);
}

void MainWindow::onClipSaved(const QString& path) {
    showToast(QStringLiteral("Клип сохранён: %1").arg(QFileInfo(path).fileName()));
    if (tray_ != nullptr && settings_.notifications.enabled) {
        tray_->showMessage(QStringLiteral("LastFrame"), QStringLiteral("Клип сохранён"), QSystemTrayIcon::Information, 2500);
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
    if (!recorder_.ffmpegPath().isEmpty() && ffmpegLabel_ != nullptr) {
        ffmpegLabel_->setText(QStringLiteral("FFmpeg: %1\nКаталог сегментов: %2")
                                  .arg(recorder_.ffmpegPath(), recorder_.temporaryDirectory()));
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
    statusDetails_->setText(text);
    statusLabel_->setStyleSheet(isError ? QStringLiteral("color: #D94841;") : QStringLiteral("color: #ED760E;"));
}

void MainWindow::setupTray() {
    tray_ = new QSystemTrayIcon(this);
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setBrush(QColor(QString::fromLatin1(accent)));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(3, 3, 26, 26, 7, 7);
    tray_->setIcon(QIcon(pixmap));
    tray_->setToolTip(QStringLiteral("LastFrame"));
    auto* menu = new QMenu(this);
    auto* save = menu->addAction(QStringLiteral("Сохранить клип"));
    auto* toggle = menu->addAction(QStringLiteral("Начать/остановить буфер"));
    auto* pause = menu->addAction(QStringLiteral("Пауза/продолжение"));
    menu->addSeparator();
    auto* clear = menu->addAction(QStringLiteral("Очистить буфер"));
    auto* settings = menu->addAction(QStringLiteral("Настройки"));
    auto* quit = menu->addAction(QStringLiteral("Выход"));
    connect(save, &QAction::triggered, this, &MainWindow::saveClip);
    connect(toggle, &QAction::triggered, this, &MainWindow::startOrStop);
    connect(pause, &QAction::triggered, this, &MainWindow::pauseOrResume);
    connect(clear, &QAction::triggered, this, &MainWindow::clearBuffer);
    connect(settings, &QAction::triggered, this, &MainWindow::showFromSingleInstance);
    connect(quit, &QAction::triggered, this, [this] { forceQuit_ = true; qApp->quit(); });
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            showFromSingleInstance();
        }
    });
    tray_->show();
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
    monitors_ = Platform::MonitorEnumerator::enumerate();
    monitorCombo_->clear();
    for (const auto& monitor : monitors_) {
        monitorCombo_->addItem(QStringLiteral("%1 — %2x%3 @ %4 Hz")
                                   .arg(monitor.name)
                                   .arg(monitor.resolution.width())
                                   .arg(monitor.resolution.height())
                                   .arg(monitor.refreshRate),
                               monitor.id);
    }
    const int selected = Platform::MonitorEnumerator::indexForId(
        monitors_, QString::fromStdString(settings_.capture.monitorId));
    if (selected >= 0) {
        monitorCombo_->setCurrentIndex(selected);
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

} // namespace LastFrame::UI
