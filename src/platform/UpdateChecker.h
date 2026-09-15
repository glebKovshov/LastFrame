#pragma once

#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace LastFrame::Platform {

class UpdateChecker final : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);

    [[nodiscard]] static QUrl endpoint();

public slots:
    void check();

signals:
    void updateAvailable(const QString& version, const QUrl& releaseUrl);
    void upToDate();
    void noRelease();
    void error(const QString& message);

private:
    QNetworkAccessManager* manager_ = nullptr;
    QNetworkReply* reply_ = nullptr;
};

} // namespace LastFrame::Platform
