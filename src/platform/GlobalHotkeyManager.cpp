#include "platform/GlobalHotkeyManager.h"

#include <QApplication>
#include <QMetaObject>
#include <QKeySequence>
#include <QStringList>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MAC)
#include <Carbon/Carbon.h>
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

#if defined(Q_OS_MAC)
bool parseMacHotkey(const QString& sequence, UInt32& modifiers, UInt32& keyCode) {
    modifiers = 0;
    keyCode = 0;
    const auto parts = sequence.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return false;
    }
    static constexpr UInt32 letterCodes[26] = {
        0, 11, 8, 2, 14, 3, 5, 4, 34, 38, 40, 37, 46,
        45, 31, 35, 12, 15, 1, 17, 32, 9, 13, 7, 16, 6,
    };
    static constexpr UInt32 digitCodes[10] = {29, 18, 19, 20, 21, 23, 22, 26, 28, 25};
    for (const QString& rawPart : parts) {
        const QString part = rawPart.trimmed().toUpper();
        if (part == QStringLiteral("CTRL") || part == QStringLiteral("CONTROL")) {
            modifiers |= controlKey;
        } else if (part == QStringLiteral("SHIFT")) {
            modifiers |= shiftKey;
        } else if (part == QStringLiteral("ALT") || part == QStringLiteral("OPTION")) {
            modifiers |= optionKey;
        } else if (part == QStringLiteral("META") || part == QStringLiteral("CMD") ||
                   part == QStringLiteral("COMMAND")) {
            modifiers |= cmdKey;
        } else if (part.startsWith(QStringLiteral("F"))) {
            bool ok = false;
            const int functionNumber = part.mid(1).toInt(&ok);
            if (!ok || functionNumber < 1 || functionNumber > 20) {
                return false;
            }
            static constexpr UInt32 functionCodes[20] = {
                kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6, kVK_F7,
                kVK_F8, kVK_F9, kVK_F10, kVK_F11, kVK_F12, kVK_F13, kVK_F14,
                kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20,
            };
            keyCode = functionCodes[functionNumber - 1];
        } else if (part.size() == 1 && part.at(0) >= QLatin1Char('A') && part.at(0) <= QLatin1Char('Z')) {
            keyCode = letterCodes[part.at(0).unicode() - QChar('A').unicode()];
        } else if (part.size() == 1 && part.at(0) >= QLatin1Char('0') && part.at(0) <= QLatin1Char('9')) {
            keyCode = digitCodes[part.at(0).unicode() - QChar('0').unicode()];
        } else {
            return false;
        }
    }
    return keyCode != 0 && modifiers != 0;
}

OSStatus macHotkeyHandler(EventHandlerCallRef, EventRef event, void* userData) {
    EventHotKeyID hotKeyId{};
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                          sizeof(hotKeyId), nullptr, &hotKeyId) != noErr || userData == nullptr) {
        return eventNotHandledErr;
    }
    auto* manager = static_cast<GlobalHotkeyManager*>(userData);
    QMetaObject::invokeMethod(manager, [manager, id = static_cast<int>(hotKeyId.id)] {
        manager->emitMacHotkey(id);
    }, Qt::QueuedConnection);
    return noErr;
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
#elif defined(Q_OS_MAC)
    for (const RegisteredHotkey& hotkey : registered_) {
        if (hotkey.nativeHandle != nullptr) {
            UnregisterEventHotKey(reinterpret_cast<EventHotKeyRef>(hotkey.nativeHandle));
        }
    }
    if (nativeEventHandler_ != nullptr) {
        RemoveEventHandler(reinterpret_cast<EventHandlerRef>(nativeEventHandler_));
        nativeEventHandler_ = nullptr;
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
        emit registrationError(QStringLiteral("[hotkey_conflict] Invalid global hotkey: %1").arg(sequence));
        return false;
    }
    if (!RegisterHotKey(nullptr, id, modifiers | MOD_NOREPEAT, key)) {
        emit registrationError(QStringLiteral("[hotkey_conflict] Global hotkey is unavailable: %1").arg(sequence));
        return false;
    }
    registered_.push_back({id, action});
    return true;
#elif defined(Q_OS_MAC)
    UInt32 modifiers = 0;
    UInt32 keyCode = 0;
    if (!parseMacHotkey(sequence, modifiers, keyCode)) {
        emit registrationError(QStringLiteral("[hotkey_conflict] Invalid global hotkey: %1").arg(sequence));
        return false;
    }
    if (nativeEventHandler_ == nullptr) {
        const EventTypeSpec eventType{ kEventClassKeyboard, kEventHotKeyPressed };
        EventHandlerRef handler = nullptr;
        if (InstallApplicationEventHandler(&macHotkeyHandler, 1, &eventType, this, &handler) != noErr) {
            emit registrationError(QStringLiteral("[hotkey_conflict] Accessibility/Input Monitoring permission is required for global hotkeys."));
            return false;
        }
        nativeEventHandler_ = reinterpret_cast<void*>(handler);
    }
    EventHotKeyID hotKeyId{ 'LFRM', static_cast<UInt32>(id) };
    EventHotKeyRef nativeHandle = nullptr;
    if (RegisterEventHotKey(keyCode, modifiers, hotKeyId, GetApplicationEventTarget(), 0, &nativeHandle) != noErr) {
        emit registrationError(QStringLiteral("[hotkey_conflict] Global hotkey is unavailable: %1").arg(sequence));
        return false;
    }
    registered_.push_back({id, action, reinterpret_cast<void*>(nativeHandle)});
    return true;
#else
    Q_UNUSED(id)
    Q_UNUSED(action)
    Q_UNUSED(sequence)
    emit registrationError(QStringLiteral("[hotkey_conflict] Global hotkeys are not implemented on this platform yet."));
    return false;
#endif
}

#if defined(Q_OS_MAC)
void GlobalHotkeyManager::emitMacHotkey(const int id) {
    for (const RegisteredHotkey& hotkey : registered_) {
        if (hotkey.id == id) {
            emit activated(hotkey.action);
            return;
        }
    }
}
#endif

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
