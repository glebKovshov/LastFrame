#include "platform/GlobalHotkeyManager.h"

#include <QApplication>
#include <QKeySequence>
#include <QStringList>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace LastFrame::Platform {
namespace {

#if defined(Q_OS_WIN)
bool parseHotkey(const QString& sequence, UINT& modifiers, UINT& key) {
    modifiers = 0;
    key = 0;
    const auto parts = sequence.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return false;
    }
    for (const QString& rawPart : parts) {
        const QString part = rawPart.trimmed().toUpper();
        if (part == QStringLiteral("CTRL") || part == QStringLiteral("CONTROL")) {
            modifiers |= MOD_CONTROL;
        } else if (part == QStringLiteral("SHIFT")) {
            modifiers |= MOD_SHIFT;
        } else if (part == QStringLiteral("ALT")) {
            modifiers |= MOD_ALT;
        } else if (part == QStringLiteral("WIN") || part == QStringLiteral("META")) {
            modifiers |= MOD_WIN;
        } else if (part.startsWith(QStringLiteral("F"))) {
            bool ok = false;
            const int functionNumber = part.mid(1).toInt(&ok);
            if (!ok || functionNumber < 1 || functionNumber > 24) {
                return false;
            }
            key = VK_F1 + functionNumber - 1;
        } else if (part.size() == 1) {
            const QChar character = part.at(0);
            if (character >= QLatin1Char('A') && character <= QLatin1Char('Z')) {
                key = character.toLatin1();
            } else if (character >= QLatin1Char('0') && character <= QLatin1Char('9')) {
                key = character.toLatin1();
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    return key != 0 && modifiers != 0;
}
#endif

} // namespace

GlobalHotkeyManager::GlobalHotkeyManager(QObject* parent) : QObject(parent) {}

GlobalHotkeyManager::~GlobalHotkeyManager() {
    unregisterHotkeys();
}

bool GlobalHotkeyManager::registerHotkeys(const QString& save, const QString& clear,
                                          const QString& pause, const QString& toggleCapture,
                                          const QString& muteMicrophone) {
    unregisterHotkeys();
    qApp->installNativeEventFilter(this);
    const bool ok = registerOne(1, HotkeyAction::Save, save) &&
                    registerOne(2, HotkeyAction::Clear, clear) &&
                    registerOne(3, HotkeyAction::Pause, pause) &&
                    registerOne(4, HotkeyAction::ToggleCapture, toggleCapture) &&
                    registerOne(5, HotkeyAction::MuteMicrophone, muteMicrophone);
    if (!ok) {
        unregisterHotkeys();
    }
    return ok;
}

void GlobalHotkeyManager::unregisterHotkeys() {
#if defined(Q_OS_WIN)
    for (const RegisteredHotkey& hotkey : registered_) {
        UnregisterHotKey(nullptr, hotkey.id);
    }
#endif
    registered_.clear();
    if (qApp != nullptr) {
        qApp->removeNativeEventFilter(this);
    }
}

bool GlobalHotkeyManager::registerOne(const int id, const HotkeyAction action,
                                      const QString& sequence) {
    if (sequence.trimmed().isEmpty()) {
        return true;
    }

#if defined(Q_OS_WIN)
    UINT modifiers = 0;
    UINT key = 0;
    if (!parseHotkey(sequence, modifiers, key)) {
        emit registrationError(QStringLiteral("Invalid global hotkey: %1").arg(sequence));
        return false;
    }
    if (!RegisterHotKey(nullptr, id, modifiers | MOD_NOREPEAT, key)) {
        emit registrationError(QStringLiteral("Global hotkey is unavailable: %1").arg(sequence));
        return false;
    }
    registered_.push_back({id, action});
    return true;
#else
    Q_UNUSED(id)
    Q_UNUSED(action)
    Q_UNUSED(sequence)
    emit registrationError(QStringLiteral("Global hotkeys are not implemented on this platform yet."));
    return false;
#endif
}

bool GlobalHotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message,
                                            qintptr* result) {
#if defined(Q_OS_WIN)
    Q_UNUSED(eventType)
    Q_UNUSED(result)
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg != nullptr && msg->message == WM_HOTKEY) {
        for (const RegisteredHotkey& hotkey : registered_) {
            if (hotkey.id == static_cast<int>(msg->wParam)) {
                emit activated(hotkey.action);
                return true;
            }
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return false;
}

} // namespace LastFrame::Platform
