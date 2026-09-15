#include "platform/AudioDeviceEnumerator.h"

#include <QProcess>
#include <QRegularExpression>

#if defined(Q_OS_WIN)
#include <Windows.h>
#include <mmdeviceapi.h>
#include <propkey.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>

#pragma comment(lib, "ole32.lib")
#endif

namespace LastFrame::Platform {
namespace {

QVector<AudioDeviceInfo> enumerate(const QString& ffmpegPath, const QString& format) {
    if (ffmpegPath.isEmpty()) {
        return {};
    }
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(ffmpegPath, {QStringLiteral("-hide_banner"), QStringLiteral("-f"), format,
                             QStringLiteral("-list_devices"), QStringLiteral("true"), QStringLiteral("-i"),
                             QStringLiteral("dummy")});
    if (!probe.waitForFinished(3000)) {
        probe.kill();
        return {};
    }

    const QString output = QString::fromUtf8(probe.readAll());
    const QRegularExpression friendlyExpression(QStringLiteral("\\\"([^\\\"]+)\\\"\\s+\\(audio\\)"));
    const QRegularExpression alternativeExpression(QStringLiteral("Alternative name \\\"([^\\\"]+)\\\""));
    QVector<AudioDeviceInfo> result;
    QString pendingName;
    for (const QString& line : output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
        const auto friendlyMatch = friendlyExpression.match(line);
        if (friendlyMatch.hasMatch()) {
            pendingName = friendlyMatch.captured(1);
            continue;
        }
        const auto alternativeMatch = alternativeExpression.match(line);
        if (alternativeMatch.hasMatch() && !pendingName.isEmpty()) {
            result.push_back({alternativeMatch.captured(1), pendingName});
            pendingName.clear();
        }
    }
    if (!pendingName.isEmpty()) {
        result.push_back({pendingName, pendingName});
    }
    return result;
}

#if defined(Q_OS_WIN)
QVector<AudioDeviceInfo> enumerateNativeOutputs() {
    QVector<AudioDeviceInfo> result;
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        return result;
    }
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT resultCode = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&enumerator));
    Microsoft::WRL::ComPtr<IMMDeviceCollection> collection;
    if (SUCCEEDED(resultCode)) {
        resultCode = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    }
    if (SUCCEEDED(resultCode)) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT index = 0; index < count; ++index) {
            Microsoft::WRL::ComPtr<IMMDevice> device;
            if (FAILED(collection->Item(index, &device))) {
                continue;
            }
            LPWSTR id = nullptr;
            if (FAILED(device->GetId(&id)) || id == nullptr) {
                continue;
            }
            QString deviceId = QString::fromWCharArray(id);
            CoTaskMemFree(id);
            Microsoft::WRL::ComPtr<IPropertyStore> properties;
            if (FAILED(device->OpenPropertyStore(STGM_READ, &properties))) {
                continue;
            }
            PROPVARIANT value;
            PropVariantInit(&value);
            QString name;
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
                value.pwszVal != nullptr) {
                name = QString::fromWCharArray(value.pwszVal);
            }
            PropVariantClear(&value);
            if (!deviceId.isEmpty() && !name.isEmpty()) {
                result.push_back({deviceId, name});
            }
        }
    }
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
    return result;
}
#endif

} // namespace

QVector<AudioDeviceInfo> AudioDeviceEnumerator::microphones(const QString& ffmpegPath) {
#if defined(Q_OS_WIN)
    return enumerate(ffmpegPath, QStringLiteral("dshow"));
#else
    Q_UNUSED(ffmpegPath)
    return {};
#endif
}

QVector<AudioDeviceInfo> AudioDeviceEnumerator::systemOutputs(const QString& ffmpegPath) {
#if defined(Q_OS_WIN)
    const QVector<AudioDeviceInfo> native = enumerateNativeOutputs();
    if (!native.isEmpty()) {
        return native;
    }
    return enumerate(ffmpegPath, QStringLiteral("wasapi"));
#else
    Q_UNUSED(ffmpegPath)
    return {};
#endif
}

} // namespace LastFrame::Platform
