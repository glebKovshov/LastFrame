#include "platform/WindowsAudioCapture.h"

#if !defined(Q_OS_WIN)
#error "WindowsAudioCapture is a Windows-only backend"
#endif

#include "core/AudioMixer.h"

#include <QUuid>

#include <Windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <propkey.h>
#include <propidl.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace LastFrame::Platform {

namespace {

constexpr PROPERTYKEY kAudioEndpointGuidKey{
    {0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}},
    4};

[[nodiscard]] QString hresultText(const HRESULT result) {
    return QStringLiteral("0x%1").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}

[[nodiscard]] QString win32Text(const DWORD error) {
    return QStringLiteral("Win32 error %1").arg(error);
}

[[nodiscard]] bool isAutoDevice(const QString& id) {
    return id.trimmed().isEmpty() || id.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] int subtypeData1(const WAVEFORMATEX* format) {
    if (format == nullptr || format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        return format == nullptr ? 0 : format->wFormatTag;
    }
    const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    return static_cast<int>(extensible->SubFormat.Data1);
}

[[nodiscard]] float pcmSample(const std::uint8_t* bytes, const int bits, const int formatTag) noexcept {
    if (formatTag == WAVE_FORMAT_IEEE_FLOAT && bits == 32) {
        float value = 0.0F;
        std::memcpy(&value, bytes, sizeof(value));
        return std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
    }
    if (formatTag != WAVE_FORMAT_PCM && formatTag != WAVE_FORMAT_EXTENSIBLE) {
        return 0.0F;
    }
    switch (bits) {
    case 8:
        return (static_cast<float>(*bytes) - 128.0F) / 128.0F;
    case 16: {
        std::int16_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return static_cast<float>(value) / 32'768.0F;
    }
    case 24: {
        const std::int32_t value = (static_cast<std::int32_t>(bytes[0]) |
                                    (static_cast<std::int32_t>(bytes[1]) << 8) |
                                    (static_cast<std::int32_t>(bytes[2]) << 16));
        const std::int32_t signedValue = (value & 0x0080'0000) != 0 ? value | 0xFF00'0000 : value;
        return static_cast<float>(signedValue) / 8'388'608.0F;
    }
    case 32: {
        std::int32_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return static_cast<float>(value) / 2'147'483'648.0F;
    }
    default:
        return 0.0F;
    }
}

} // namespace

class WindowsAudioCapture::Impl final {
public:
    explicit Impl(WindowsAudioCapture* owner) : owner_(owner) {}

    ~Impl() { stop(); }

    bool prepare(const Config& config, QString* error) {
        stop();
        if (config.sampleRate < 8'000 || config.sampleRate > 192'000 ||
            (!config.systemEnabled && !config.microphoneEnabled)) {
            setError(error, QStringLiteral("Invalid native audio configuration"));
            return false;
        }
        config_ = config;
        mixer_ = std::make_unique<Core::AudioMixer>(config.sampleRate, 2, std::max(1, config.sampleRate / 100));
        mixer_->setVolumes(config.systemVolume, config.microphoneVolume);
        mixer_->setSystemEnabled(config.systemEnabled);
        mixer_->setMicrophoneEnabled(config.microphoneEnabled);
        microphoneMuted_.store(config.microphoneMuted, std::memory_order_relaxed);

        pipePath_ = QStringLiteral("\\\\.\\pipe\\LastFrameAudio_%1")
                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        pipe_ = CreateNamedPipeW(reinterpret_cast<LPCWSTR>(pipePath_.utf16()),
                                 PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1,
                                 512U * 1024U,
                                 512U * 1024U,
                                 0,
                                 nullptr);
        if (pipe_ == INVALID_HANDLE_VALUE) {
            pipe_ = nullptr;
            setError(error, win32Text(GetLastError()));
            return false;
        }
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stopEvent_ == nullptr) {
            setError(error, win32Text(GetLastError()));
            closePipe();
            return false;
        }
        prepared_ = true;
        return true;
    }

    bool start() {
        if (!prepared_ || running_) {
            return false;
        }
        running_ = true;
        worker_ = std::thread([this] { captureLoop(); });
        return true;
    }

    void stop() {
        if (stopEvent_ != nullptr) {
            SetEvent(stopEvent_);
        }
        if (pipe_ != nullptr && pipe_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(pipe_, nullptr);
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        running_ = false;
        prepared_ = false;
        closePipe();
        pipePath_.clear();
        mixer_.reset();
        microphoneMuted_.store(false, std::memory_order_relaxed);
    }

    void setMicrophoneMuted(const bool muted) {
        microphoneMuted_.store(muted, std::memory_order_relaxed);
    }

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] QString inputPath() const { return pipePath_; }

private:
    struct Source final {
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> capture;
        WAVEFORMATEX* format = nullptr;
        bool loopback = false;
        std::optional<Core::Timestamp> nextPts;

        ~Source() {
            if (format != nullptr) {
                CoTaskMemFree(format);
            }
        }
    };

    static void setError(QString* error, const QString& value) {
        if (error != nullptr) {
            *error = value;
        }
    }

    void closePipe() {
        if (pipe_ != nullptr && pipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_);
        }
        pipe_ = nullptr;
        if (stopEvent_ != nullptr) {
            CloseHandle(stopEvent_);
        }
        stopEvent_ = nullptr;
    }

    void fail(const QString& reason) {
        if (stopEvent_ != nullptr && WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0) {
            return;
        }
        emit owner_->failed(reason);
    }

    void degrade(const QString& reason) {
        if (stopEvent_ != nullptr && WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0) {
            return;
        }
        emit owner_->degraded(reason);
    }

    bool connectPipe() {
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr) {
            fail(win32Text(GetLastError()));
            return false;
        }
        const BOOL connected = ConnectNamedPipe(pipe_, &overlapped);
        const DWORD connectError = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && connectError != ERROR_IO_PENDING && connectError != ERROR_PIPE_CONNECTED) {
            CloseHandle(overlapped.hEvent);
            fail(win32Text(connectError));
            return false;
        }
        if (!connected && connectError == ERROR_PIPE_CONNECTED) {
            SetEvent(overlapped.hEvent);
        }
        const HANDLE handles[] = {overlapped.hEvent, stopEvent_};
        const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0 + 1) {
            CancelIoEx(pipe_, &overlapped);
            CloseHandle(overlapped.hEvent);
            return false;
        }
        if (waitResult != WAIT_OBJECT_0) {
            CloseHandle(overlapped.hEvent);
            fail(win32Text(GetLastError()));
            return false;
        }
        DWORD transferred = 0;
        const bool complete = GetOverlappedResult(pipe_, &overlapped, &transferred, FALSE) != FALSE;
        CloseHandle(overlapped.hEvent);
        if (!complete && connectError != ERROR_PIPE_CONNECTED) {
            fail(win32Text(GetLastError()));
            return false;
        }
        return true;
    }

    bool writeBytes(const std::uint8_t* bytes, const std::size_t size) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr) {
            fail(win32Text(GetLastError()));
            return false;
        }
        const BOOL written = WriteFile(pipe_, bytes, static_cast<DWORD>(size), nullptr, &overlapped);
        const DWORD writeError = written ? ERROR_SUCCESS : GetLastError();
        if (!written && writeError != ERROR_IO_PENDING) {
            CloseHandle(overlapped.hEvent);
            if (writeError != ERROR_OPERATION_ABORTED) {
                fail(win32Text(writeError));
            }
            return false;
        }
        const HANDLE handles[] = {overlapped.hEvent, stopEvent_};
        const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0 + 1) {
            CancelIoEx(pipe_, &overlapped);
            CloseHandle(overlapped.hEvent);
            return false;
        }
        if (waitResult != WAIT_OBJECT_0) {
            CancelIoEx(pipe_, &overlapped);
            CloseHandle(overlapped.hEvent);
            fail(win32Text(GetLastError()));
            return false;
        }
        DWORD transferred = 0;
        const bool complete = GetOverlappedResult(pipe_, &overlapped, &transferred, FALSE) != FALSE &&
                              transferred == size;
        const DWORD resultError = complete ? ERROR_SUCCESS : GetLastError();
        CloseHandle(overlapped.hEvent);
        if (!complete && resultError != ERROR_OPERATION_ABORTED) {
            fail(win32Text(resultError));
        }
        return complete;
    }

    [[nodiscard]] QString endpointFriendlyName(IMMDevice* device) const {
        ComPtr<IPropertyStore> properties;
        if (device == nullptr || FAILED(device->OpenPropertyStore(STGM_READ, &properties))) {
            return {};
        }
        PROPVARIANT value;
        PropVariantInit(&value);
        QString result;
        if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.pwszVal != nullptr) {
            result = QString::fromWCharArray(value.pwszVal);
        }
        PropVariantClear(&value);
        return result;
    }

    [[nodiscard]] QString endpointGuid(IMMDevice* device) const {
        ComPtr<IPropertyStore> properties;
        if (device == nullptr || FAILED(device->OpenPropertyStore(STGM_READ, &properties))) {
            return {};
        }
        PROPVARIANT value;
        PropVariantInit(&value);
        QString result;
        if (SUCCEEDED(properties->GetValue(kAudioEndpointGuidKey, &value))) {
            if (value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
                result = QString::fromWCharArray(value.pwszVal);
            } else if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
                result = QString::fromWCharArray(value.bstrVal);
            } else if (value.vt == VT_CLSID && value.puuid != nullptr) {
                wchar_t guid[64] = {};
                if (StringFromGUID2(*value.puuid, guid,
                                    static_cast<int>(sizeof(guid) / sizeof(guid[0]))) > 0) {
                    result = QString::fromWCharArray(guid);
                }
            }
        }
        PropVariantClear(&value);
        return result;
    }

    ComPtr<IMMDevice> resolveEndpoint(IMMDeviceEnumerator* enumerator,
                                      const EDataFlow flow,
                                      const QString& requested) const {
        ComPtr<IMMDevice> device;
        if (isAutoDevice(requested)) {
            enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
            return device;
        }
        if (SUCCEEDED(enumerator->GetDevice(reinterpret_cast<LPCWSTR>(requested.utf16()), &device))) {
            return device;
        }
        ComPtr<IMMDeviceCollection> collection;
        if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection))) {
            return {};
        }
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT index = 0; index < count; ++index) {
            ComPtr<IMMDevice> candidate;
            if (FAILED(collection->Item(index, &candidate))) {
                continue;
            }
            LPWSTR id = nullptr;
            const HRESULT idResult = candidate->GetId(&id);
            const QString candidateId = id == nullptr ? QString() : QString::fromWCharArray(id);
            if (id != nullptr) {
                CoTaskMemFree(id);
            }
            const QString friendlyName = endpointFriendlyName(candidate.Get());
            const QString audioEndpointGuid = endpointGuid(candidate.Get());
            if (candidateId == requested || friendlyName == requested ||
                friendlyName.contains(requested, Qt::CaseInsensitive) ||
                (!audioEndpointGuid.isEmpty() && requested.contains(audioEndpointGuid, Qt::CaseInsensitive))) {
                return candidate;
            }
            Q_UNUSED(idResult)
        }
        return {};
    }

    bool openSource(IMMDeviceEnumerator* enumerator,
                    const EDataFlow flow,
                    const QString& requested,
                    const bool loopback,
                    Source& source,
                    QString* error) {
        ComPtr<IMMDevice> device = resolveEndpoint(enumerator, flow, requested);
        if (device == nullptr) {
            setError(error, QStringLiteral("Audio endpoint was not found"));
            return false;
        }
        HRESULT result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                          reinterpret_cast<void**>(source.client.GetAddressOf()));
        if (FAILED(result)) {
            setError(error, QStringLiteral("Audio client activation failed: %1").arg(hresultText(result)));
            return false;
        }
        result = source.client->GetMixFormat(&source.format);
        if (FAILED(result) || source.format == nullptr) {
            setError(error, QStringLiteral("Audio mix format is unavailable: %1").arg(hresultText(result)));
            return false;
        }
        const int channels = source.format->nChannels;
        if (channels < 1 || channels > 8 || source.format->nBlockAlign == 0 || source.format->nSamplesPerSec == 0) {
            setError(error, QStringLiteral("Unsupported audio endpoint format"));
            return false;
        }
        const DWORD flags = loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
        result = source.client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, source.format, nullptr);
        if (FAILED(result)) {
            setError(error, QStringLiteral("Audio client initialization failed: %1").arg(hresultText(result)));
            return false;
        }
        result = source.client->GetService(IID_PPV_ARGS(&source.capture));
        if (FAILED(result)) {
            setError(error, QStringLiteral("Audio capture service is unavailable: %1").arg(hresultText(result)));
            return false;
        }
        result = source.client->Start();
        if (FAILED(result)) {
            setError(error, QStringLiteral("Audio client start failed: %1").arg(hresultText(result)));
            return false;
        }
        source.loopback = loopback;
        return true;
    }

    bool readSource(Source& source,
                    const bool systemSource,
                    const Core::Timestamp nowPts,
                    QString* error) {
        UINT packetFrames = 0;
        HRESULT result = source.capture->GetNextPacketSize(&packetFrames);
        if (FAILED(result)) {
            setError(error, QStringLiteral("WASAPI packet query failed: %1").arg(hresultText(result)));
            return false;
        }
        while (packetFrames > 0) {
            BYTE* data = nullptr;
            UINT frames = 0;
            DWORD flags = 0;
            result = source.capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(result)) {
                setError(error, QStringLiteral("WASAPI buffer acquisition failed: %1").arg(hresultText(result)));
                return false;
            }
            Core::AudioBlock block;
            block.startPts = source.nextPts.value_or(nowPts);
            block.sampleRate = static_cast<int>(source.format->nSamplesPerSec);
            block.channels = static_cast<int>(source.format->nChannels);
            block.samples.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(block.channels), 0.0F);
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && data != nullptr) {
                const int formatTag = subtypeData1(source.format);
                const int bytesPerSample = source.format->wBitsPerSample / 8;
                for (UINT frame = 0; frame < frames; ++frame) {
                    for (int channel = 0; channel < block.channels; ++channel) {
                        const auto* sample = data + static_cast<std::size_t>(frame) * source.format->nBlockAlign +
                                             static_cast<std::size_t>(channel) * bytesPerSample;
                        block.samples[static_cast<std::size_t>(frame) * static_cast<std::size_t>(block.channels) +
                                      static_cast<std::size_t>(channel)] = pcmSample(sample, source.format->wBitsPerSample,
                                                                                      formatTag);
                    }
                }
            }
            source.capture->ReleaseBuffer(frames);
            source.nextPts = block.endPts();
            if (systemSource) {
                mixer_->pushSystem(std::move(block));
            } else {
                mixer_->pushMicrophone(std::move(block));
            }
            result = source.capture->GetNextPacketSize(&packetFrames);
            if (FAILED(result)) {
                setError(error, QStringLiteral("WASAPI packet query failed: %1").arg(hresultText(result)));
                return false;
            }
        }
        return true;
    }

    bool writeMixed(const std::vector<Core::MixedAudioBlock>& blocks) {
        for (const Core::MixedAudioBlock& block : blocks) {
            if (!block.samples.empty() &&
                !writeBytes(reinterpret_cast<const std::uint8_t*>(block.samples.data()),
                            block.samples.size() * sizeof(float))) {
                return false;
            }
        }
        return true;
    }

    void captureLoop() {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
            fail(QStringLiteral("COM initialization failed: %1").arg(hresultText(comResult)));
            return;
        }
        DWORD taskIndex = 0;
        HANDLE avrtTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        if (!connectPipe()) {
            if (avrtTask != nullptr) {
                AvRevertMmThreadCharacteristics(avrtTask);
            }
            if (SUCCEEDED(comResult)) {
                CoUninitialize();
            }
            return;
        }

        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&enumerator));
        if (FAILED(result)) {
            fail(QStringLiteral("MMDevice enumerator creation failed: %1").arg(hresultText(result)));
        }
        Source systemSource;
        Source microphoneSource;
        bool systemActive = false;
        bool microphoneActive = false;
        if (SUCCEEDED(result) && config_.systemEnabled) {
            QString error;
            systemActive = openSource(enumerator.Get(), eRender, config_.systemDeviceId, true, systemSource, &error);
            if (!systemActive) {
                degrade(QStringLiteral("Системный WASAPI loopback недоступен: %1").arg(error));
            }
        }
        if (SUCCEEDED(result) && config_.microphoneEnabled) {
            QString error;
            microphoneActive = openSource(enumerator.Get(), eCapture, config_.microphoneDeviceId, false,
                                          microphoneSource, &error);
            if (!microphoneActive) {
                degrade(QStringLiteral("Микрофонный WASAPI capture недоступен: %1").arg(error));
            }
        }
        if (!systemActive && !microphoneActive) {
            fail(QStringLiteral("No WASAPI audio source could be started"));
        } else {
            const auto startedAt = std::chrono::steady_clock::now();
            mixer_->start(0);
            while (WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - startedAt);
                const Core::Timestamp nowPts = elapsed.count();
                if (systemActive) {
                    QString error;
                    if (!readSource(systemSource, true, nowPts, &error)) {
                        systemActive = false;
                        degrade(QStringLiteral("Системный WASAPI endpoint отключён: %1").arg(error));
                    }
                }
                if (microphoneActive) {
                    QString error;
                    if (!readSource(microphoneSource, false, nowPts, &error)) {
                        microphoneActive = false;
                        degrade(QStringLiteral("Микрофонный WASAPI endpoint отключён: %1").arg(error));
                    }
                }
                if (!systemActive && !microphoneActive) {
                    fail(QStringLiteral("All WASAPI audio sources were disconnected"));
                    break;
                }
                mixer_->setMicrophoneMuted(microphoneMuted_.load(std::memory_order_relaxed));
                if (!writeMixed(mixer_->drainUntil(nowPts))) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (systemSource.client != nullptr) {
                systemSource.client->Stop();
            }
            if (microphoneSource.client != nullptr) {
                microphoneSource.client->Stop();
            }
        }
        if (avrtTask != nullptr) {
            AvRevertMmThreadCharacteristics(avrtTask);
        }
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
    }

    WindowsAudioCapture* owner_ = nullptr;
    Config config_;
    HANDLE pipe_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    QString pipePath_;
    std::thread worker_;
    std::unique_ptr<Core::AudioMixer> mixer_;
    std::atomic<bool> microphoneMuted_{false};
    bool prepared_ = false;
    bool running_ = false;
};

WindowsAudioCapture::WindowsAudioCapture(QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(this)) {}

WindowsAudioCapture::~WindowsAudioCapture() = default;

bool WindowsAudioCapture::prepare(const Config& config, QString* error) {
    return impl_->prepare(config, error);
}

bool WindowsAudioCapture::start() {
    return impl_->start();
}

void WindowsAudioCapture::stop() {
    impl_->stop();
}

void WindowsAudioCapture::setMicrophoneMuted(const bool muted) {
    impl_->setMicrophoneMuted(muted);
}

bool WindowsAudioCapture::isPrepared() const noexcept {
    return impl_->isPrepared();
}

QString WindowsAudioCapture::inputPath() const {
    return impl_->inputPath();
}

} // namespace LastFrame::Platform
