#include "ui/NotificationOverlay.h"

#include <QApplication>
#include <QCoreApplication>
#include <QScreen>

#include <iostream>

#if defined(Q_OS_WIN)
#include <Windows.h>
#endif

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    LastFrame::UI::NotificationOverlay overlay;
    if (const QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        overlay.setMonitorGeometry(screen->geometry());
    }
    overlay.showMessage(QStringLiteral("overlay smoke"), false, 1000);
    QCoreApplication::processEvents();

#if defined(Q_OS_WIN)
    DWORD affinity = 0;
    const bool queried = GetWindowDisplayAffinity(reinterpret_cast<HWND>(overlay.winId()), &affinity) != FALSE;
    if (!queried || affinity != WDA_EXCLUDEFROMCAPTURE) {
        std::cerr << "Notification overlay is not excluded from capture\n";
        return 1;
    }
    std::cout << "Notification overlay capture exclusion passed\n";
#else
    std::cout << "Notification overlay smoke skipped: Windows display affinity is platform-specific\n";
#endif
    return 0;
}
