#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>

namespace LastFrame::Platform {

enum class HotkeyAction {
    Save,
    Clear,
    Pause,
    ToggleCapture,
    MuteMicrophone,
};

class GlobalHotkeyManager final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit GlobalHotkeyManager(QObject* parent = nullptr);
    ~GlobalHotkeyManager() override;

    GlobalHotkeyManager(const GlobalHotkeyManager&) = delete;
    GlobalHotkeyManager& operator=(const GlobalHotkeyManager&) = delete;

    [[nodiscard]] bool registerHotkeys(const QString& save, const QString& clear,
                                       const QString& pause, const QString& toggleCapture,
                                       const QString& muteMicrophone);
    void unregisterHotkeys();

    [[nodiscard]] bool nativeEventFilter(const QByteArray& eventType, void* message,
                                         qintptr* result) override;

signals:
    void activated(LastFrame::Platform::HotkeyAction action);
    void registrationError(const QString& message);

private:
    struct RegisteredHotkey {
        int id = 0;
        HotkeyAction action = HotkeyAction::Save;
    };

    [[nodiscard]] bool registerOne(int id, HotkeyAction action, const QString& sequence);
    QVector<RegisteredHotkey> registered_;
};

} // namespace LastFrame::Platform
