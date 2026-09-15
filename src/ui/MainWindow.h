#pragma once

#include "core/SettingsStore.h"
#include "media/PortableSegmentRecorder.h"
#include "platform/GlobalHotkeyManager.h"
#include "platform/MonitorEnumerator.h"
#include "platform/Diagnostics.h"
#include "platform/FfmpegCapabilityProbe.h"
#include "platform/UpdateChecker.h"

#include <QMainWindow>
#include <QTimer>
#include <QUrl>

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
class QAction;

namespace LastFrame::UI {

class NotificationOverlay;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

public slots:
    void showFromSingleInstance();

protected:
    void closeEvent(QCloseEvent* event) override;
#if defined(Q_OS_WIN)
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private slots:
    void startOrStop();
    void pauseOrResume();
    void saveClip();
    void clearBuffer();
    void toggleMicrophoneMute();
    void chooseRegion();
    void checkForUpdates();
    void copyDiagnostics();
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
    QWidget* buildNotificationsPage();
    QWidget* buildAdvancedPage();
    QWidget* buildAboutPage();
    void addPageButton(const QString& title, int pageIndex);
    void selectPage(int pageIndex);
    void applyTheme(bool dark);
    void applyRecorderState();
    void loadSettings();
    void saveSettings();
    void showToast(const QString& text, bool isError = false);
    void updateTrayIcon();
    void setupTray();
    void registerHotkeys();
    void populateMonitorCombo();

    [[nodiscard]] static std::filesystem::path settingsPath();

    LastFrame::Core::Settings settings_;
    LastFrame::Core::SettingsStore settingsStore_;
    LastFrame::Media::PortableSegmentRecorder recorder_;
    LastFrame::Platform::GlobalHotkeyManager hotkeys_;
    LastFrame::Platform::UpdateChecker updater_;
    LastFrame::Platform::FfmpegCapabilityProbe capabilityProbe_;
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
    QComboBox* sourceCombo_ = nullptr;
    QPushButton* regionButton_ = nullptr;
    QSpinBox* durationSpin_ = nullptr;
    QSpinBox* ramLimitSpin_ = nullptr;
    QSpinBox* fpsSpin_ = nullptr;
    QSpinBox* outputWidthSpin_ = nullptr;
    QSpinBox* outputHeightSpin_ = nullptr;
    QComboBox* containerCombo_ = nullptr;
    QComboBox* codecCombo_ = nullptr;
    QComboBox* presetCombo_ = nullptr;
    QSpinBox* bitrateSpin_ = nullptr;
    QSpinBox* maxFileSizeSpin_ = nullptr;
    QLabel* estimatedSizeLabel_ = nullptr;
    QComboBox* themeCombo_ = nullptr;
    QCheckBox* systemAudioCheck_ = nullptr;
    QCheckBox* microphoneCheck_ = nullptr;
    QPushButton* updateButton_ = nullptr;
    QPushButton* diagnosticsButton_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QAction* trayMuteAction_ = nullptr;
    NotificationOverlay* notificationOverlay_ = nullptr;
    QTimer monitorTimer_;
    QTimer autoResumeTimer_;
    QString monitorSignature_;
    QString autoResumeSignature_;
    QUrl latestReleaseUrl_;
    QString lastSavedPath_;
    QString capabilitySummary_;
    bool monitorSignatureInitialized_ = false;
    bool monitorResumePending_ = false;
    bool forceQuit_ = false;
    bool settingsRecovered_ = false;
    bool trayError_ = false;
    bool trayWarning_ = false;
    bool lifecycleWasRecording_ = false;
    bool lifecycleSuspended_ = false;
};

} // namespace LastFrame::UI
