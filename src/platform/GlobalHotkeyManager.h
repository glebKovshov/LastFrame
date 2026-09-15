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
#if defined(Q_OS_MAC)
    // Carbon delivers the event outside Qt's native event filter. The handler
    // queues back into the Qt object before dispatching the public signal.
    void emitMacHotkey(int id);
#endif

    [[nodiscard]] bool nativeEventFilter(const QByteArray& eventType, void* message,
                                         qintptr* result) override;

signals:
    void activated(LastFrame::Platform::HotkeyAction action);
    void registrationError(const QString& message);

private:
    struct RegisteredHotkey {
        int id = 0;
        HotkeyAction action = HotkeyAction::Save;
#if defined(Q_OS_MAC)
        void* nativeHandle = nullptr;
#endif
    };

    [[nodiscard]] bool registerOne(int id, HotkeyAction action, const QString& sequence);
#if defined(Q_OS_MAC)
    void* nativeEventHandler_ = nullptr;
#endif
    QVector<RegisteredHotkey> registered_;
};

} // namespace LastFrame::Platform
