#pragma once

#include "core/SettingsStore.h"
#include "media/PortableSegmentRecorder.h"
#include "platform/GlobalHotkeyManager.h"
#include "platform/MonitorEnumerator.h"

#include <QMainWindow>

#include <filesystem>

class QLabel;
class QPushButton;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QSlider;
class QStackedWidget;
class QCloseEvent;
class QSystemTrayIcon;

namespace LastFrame::UI {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

public slots:
    void showFromSingleInstance();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void startOrStop();
    void pauseOrResume();
    void saveClip();
    void clearBuffer();
    void refreshMonitors();
    void chooseTheme(int index);
    void handleHotkey(LastFrame::Platform::HotkeyAction action);
    void showRecorderError(const QString& text);
    void showRecorderMessage(const QString& text);
    void onClipSaved(const QString& path);

private:
    void buildUi();
    QWidget* buildOverviewPage();
    QWidget* buildCapturePage();
    QWidget* buildVideoPage();
    QWidget* buildAudioPage();
    QWidget* buildHotkeysPage();
    QWidget* buildStoragePage();
    QWidget* buildAdvancedPage();
    QWidget* buildAboutPage();
    void addPageButton(const QString& title, int pageIndex);
    void selectPage(int pageIndex);
    void applyTheme(bool dark);
    void applyRecorderState();
    void loadSettings();
    void saveSettings();
    void showToast(const QString& text, bool isError = false);
    void setupTray();
    void registerHotkeys();
    void populateMonitorCombo();

    [[nodiscard]] static std::filesystem::path settingsPath();

    LastFrame::Core::Settings settings_;
    LastFrame::Core::SettingsStore settingsStore_;
    LastFrame::Media::PortableSegmentRecorder recorder_;
    LastFrame::Platform::GlobalHotkeyManager hotkeys_;
    QVector<LastFrame::Platform::MonitorInfo> monitors_;

    QStackedWidget* pages_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* statusDetails_ = nullptr;
    QLabel* ffmpegLabel_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* pauseButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QComboBox* monitorCombo_ = nullptr;
    QSpinBox* durationSpin_ = nullptr;
    QSpinBox* fpsSpin_ = nullptr;
    QComboBox* themeCombo_ = nullptr;
    QCheckBox* systemAudioCheck_ = nullptr;
    QCheckBox* microphoneCheck_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    bool forceQuit_ = false;
    bool settingsRecovered_ = false;
};

} // namespace LastFrame::UI
