#include "ui/MainWindow.h"

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("LastFrame"));
    application.setOrganizationName(QStringLiteral("LastFrame"));
    application.setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setQuitOnLastWindowClosed(false);

    constexpr auto serverName = "LastFrame.SingleInstance";
    QLocalSocket probe;
    probe.connectToServer(QString::fromLatin1(serverName));
    if (probe.waitForConnected(250)) {
        probe.write("activate");
        probe.waitForBytesWritten(250);
        return 0;
    }

    QLocalServer server;
    QLocalServer::removeServer(QString::fromLatin1(serverName));
    if (!server.listen(QString::fromLatin1(serverName))) {
        return 2;
    }

    LastFrame::UI::MainWindow window;
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window] {
        while (server.hasPendingConnections()) {
            auto* socket = server.nextPendingConnection();
            socket->waitForReadyRead(100);
            socket->deleteLater();
            window.showFromSingleInstance();
        }
    });
    window.show();
    return application.exec();
}
