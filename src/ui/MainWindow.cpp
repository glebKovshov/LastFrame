#include "ui/MainWindow.h"
#include "ui/RegionSelector.h"
#include "platform/AudioDeviceEnumerator.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDesktopServices>
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
    monitorTimer_.setInterval(500);
    connect(&monitorTimer_, &QTimer::timeout, this, &MainWindow::refreshMonitors);
    monitorTimer_.start();
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
    fpsSpin_ = new QSpinBox(page);
    fpsSpin_->setRange(15, 360);
    fpsSpin_->setValue(settings_.capture.fps);
    form->addRow(QStringLiteral("FPS"), fpsSpin_);
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
    layout->addLayout(form);
    layout->addWidget(description(QStringLiteral("Регион ограничивается выбранным монитором. Параметры применяются при следующем запуске буфера."), page));
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
    connect(fpsSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) { settings_.capture.fps = value; });
    connect(outputWidthSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { settings_.capture.outputWidth = value; });
    connect(outputHeightSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { settings_.capture.outputHeight = value; });
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
    layout->addLayout(form);
    layout->addWidget(description(QStringLiteral("MP4/MKV используют H.264, WebM — VP9. Auto выбирает H.264 NVENC на Windows и сохраняет fallback через software encoder."), page));
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
                    const QSignalBlocker blocker(bitrateSpin_);
                    bitrateSpin_->setValue(bitrate);
                }
            });
    connect(bitrateSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { settings_.video.customBitrateKbps = value; });
    connect(maxFileSizeSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { settings_.video.maxFileSizeMiB = value; });
    const bool customPreset = presetCombo_->currentText().compare(QStringLiteral("Custom"), Qt::CaseInsensitive) == 0;
    bitrateSpin_->setReadOnly(!customPreset);
    if (!customPreset) {
        const QString value = presetCombo_->currentText();
        const int bitrate = value.compare(QStringLiteral("Low"), Qt::CaseInsensitive) == 0 ? 6000
                            : value.compare(QStringLiteral("Medium"), Qt::CaseInsensitive) == 0 ? 10000
                            : value.compare(QStringLiteral("Ultra"), Qt::CaseInsensitive) == 0 ? 24000 : 16000;
        bitrateSpin_->setValue(bitrate);
    }
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
    const QString adjacentFfmpeg = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ffmpeg.exe"));
    const QString ffmpegPath = QFileInfo::exists(adjacentFfmpeg) ? adjacentFfmpeg
                                                                  : QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
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
    updateButton_ = new QPushButton(QStringLiteral("Проверить обновления"), page);
    layout->addWidget(updateButton_);
    diagnosticsButton_ = new QPushButton(QStringLiteral("Скопировать диагностику"), page);
    layout->addWidget(diagnosticsButton_);
    layout->addWidget(description(QStringLiteral("Телеметрия отключена. Проверка обновлений будет ручной через GitHub Releases и по умолчанию отключена."), page));
    layout->addStretch();
    connect(durationSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) { settings_.buffer.durationSeconds = value; });
    connect(themeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::chooseTheme);
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
        signature += monitor.id + QStringLiteral("|") + QString::number(monitor.refreshRate) + QStringLiteral(";");
    }
    const bool changed = monitorSignatureInitialized_ && signature != monitorSignature_;
    monitorSignature_ = signature;
    monitorSignatureInitialized_ = true;
    monitors_ = detected;
    if (changed && recorder_.isRecording()) {
        recorder_.stop();
        showToast(QStringLiteral("Монитор или его режим изменился. Буфер остановлен; проверьте настройки и запустите снова."), true);
    }
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
    Platform::Diagnostics::append(QStringLiteral("ERROR"), text);
    showToast(text, true);
}

void MainWindow::showRecorderMessage(const QString& text) {
    Platform::Diagnostics::append(QStringLiteral("INFO"), text);
    showToast(text);
}

void MainWindow::onClipSaved(const QString& path) {
    lastSavedPath_ = path;
    Platform::Diagnostics::append(QStringLiteral("INFO"), QStringLiteral("Clip export completed."));
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
