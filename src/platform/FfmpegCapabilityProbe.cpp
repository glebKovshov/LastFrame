#include "platform/FfmpegCapabilityProbe.h"

namespace LastFrame::Platform {

FfmpegCapabilityProbe::FfmpegCapabilityProbe(QObject* parent) : QObject(parent) {
    process_.setProcessChannelMode(QProcess::MergedChannels);
    timeout_.setSingleShot(true);
    timeout_.setInterval(4000);
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            &FfmpegCapabilityProbe::handleFinished);
    connect(&process_, &QProcess::errorOccurred, this, &FfmpegCapabilityProbe::handleError);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
        }
    });
}

void FfmpegCapabilityProbe::probe(const QString& ffmpegPath) {
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
    }
    timeout_.stop();
    queryActive_ = false;
    ffmpegPath_ = ffmpegPath;
    capabilities_ = {};
    capabilities_.path = ffmpegPath;
    capabilities_.available = !ffmpegPath.isEmpty();
    queries_ = {QStringLiteral("version"), QStringLiteral("filters"), QStringLiteral("devices"),
                QStringLiteral("encoders")};
    queryIndex_ = 0;
    if (ffmpegPath_.isEmpty()) {
        emit finished(capabilities_);
        return;
    }
    runNextQuery();
}

void FfmpegCapabilityProbe::runNextQuery() {
    if (queryIndex_ >= queries_.size()) {
        emit finished(capabilities_);
        return;
    }
    const QString query = queries_.at(queryIndex_);
    QStringList arguments{QStringLiteral("-hide_banner")};
    if (query == QStringLiteral("version")) {
        arguments << QStringLiteral("-version");
    } else if (query == QStringLiteral("filters")) {
        arguments << QStringLiteral("-filters");
    } else if (query == QStringLiteral("devices")) {
        arguments << QStringLiteral("-devices");
    } else {
        arguments << QStringLiteral("-encoders");
    }
    process_.start(ffmpegPath_, arguments);
    queryActive_ = true;
    timeout_.start();
}

void FfmpegCapabilityProbe::handleFinished(const int exitCode, const QProcess::ExitStatus status) {
    if (!queryActive_) {
        return;
    }
    queryActive_ = false;
    Q_UNUSED(exitCode)
    Q_UNUSED(status)
    timeout_.stop();
    parseQueryOutput(QString::fromUtf8(process_.readAll()));
    ++queryIndex_;
    runNextQuery();
}

void FfmpegCapabilityProbe::handleError(const QProcess::ProcessError error) {
    Q_UNUSED(error)
    if (!queryActive_) {
        return;
    }
    queryActive_ = false;
    timeout_.stop();
    capabilities_.available = false;
    emit finished(capabilities_);
}

void FfmpegCapabilityProbe::parseQueryOutput(const QString& output) {
    const QString query = queries_.value(queryIndex_);
    if (query == QStringLiteral("version")) {
        const QString firstLine = output.section(QLatin1Char('\n'), 0, 0).trimmed();
        capabilities_.version = firstLine;
    } else if (query == QStringLiteral("filters")) {
        capabilities_.desktopDuplication = output.contains(QStringLiteral("ddagrab"));
    } else if (query == QStringLiteral("devices")) {
        capabilities_.gdiCapture = output.contains(QStringLiteral("gdigrab"));
        capabilities_.directShow = output.contains(QStringLiteral("dshow"));
        capabilities_.wasapi = output.contains(QStringLiteral("wasapi"));
    } else if (query == QStringLiteral("encoders")) {
        capabilities_.nvencH264 = output.contains(QStringLiteral("h264_nvenc"));
        capabilities_.softwareH264 = output.contains(QStringLiteral("libx264"));
        capabilities_.vp9 = output.contains(QStringLiteral("libvpx-vp9"));
    }
}

} // namespace LastFrame::Platform
