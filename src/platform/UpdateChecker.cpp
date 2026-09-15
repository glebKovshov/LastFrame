#include "platform/UpdateChecker.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QVersionNumber>

namespace LastFrame::Platform {

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent), manager_(new QNetworkAccessManager(this)) {}

QUrl UpdateChecker::endpoint() {
    return QUrl(QStringLiteral("https://api.github.com/repos/glebKovshov/LastFrame/releases/latest"));
}

void UpdateChecker::check() {
    if (reply_ != nullptr) {
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }

    QNetworkRequest request(endpoint());
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LastFrame/%1").arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(8000);
    reply_ = manager_->get(request);
    QPointer<QNetworkReply> guard(reply_);
    connect(reply_, &QNetworkReply::finished, this, [this, guard] {
        if (guard.isNull()) {
            return;
        }
        QNetworkReply* reply = guard.data();
        reply_ = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            if (status == 404) {
                emit noRelease();
            } else {
                emit error(QStringLiteral("Проверка обновлений не выполнена: %1").arg(reply->errorString()));
            }
            reply->deleteLater();
            return;
        }

        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit error(QStringLiteral("GitHub вернул некорректный ответ обновления."));
            reply->deleteLater();
            return;
        }
        const QJsonObject object = document.object();
        QString tag = object.value(QStringLiteral("tag_name")).toString().trimmed();
        if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
            tag.remove(0, 1);
        }
        const QVersionNumber latest = QVersionNumber::fromString(tag);
        const QVersionNumber current = QVersionNumber::fromString(QCoreApplication::applicationVersion());
        const QUrl releaseUrl(object.value(QStringLiteral("html_url")).toString());
        if (latest.isNull() || releaseUrl.isEmpty()) {
            emit error(QStringLiteral("В ответе GitHub отсутствует версия release."));
        } else if (latest > current) {
            emit updateAvailable(QStringLiteral("v%1").arg(tag), releaseUrl);
        } else {
            emit upToDate();
        }
        reply->deleteLater();
    });
}

} // namespace LastFrame::Platform
