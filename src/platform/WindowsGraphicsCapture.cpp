#include "platform/WindowsGraphicsCapture.h"

#if !defined(Q_OS_WIN)
#error "WindowsGraphicsCapture is a Windows-only backend"
#endif

#include <QUuid>

#include <Windows.h>
#include <d3d11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "runtimeobject.lib")

using Microsoft::WRL::ComPtr;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;

namespace LastFrame::Platform {

namespace {

[[nodiscard]] QString hresultText(const HRESULT result) {
    return QStringLiteral("0x%1").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}

[[nodiscard]] QString win32Text(const DWORD error) {
    return QStringLiteral("Win32 error %1").arg(error);
}

[[nodiscard]] QString winrtText(const winrt::hresult_error& error) {
    return QStringLiteral("%1 (%2)")
        .arg(QString::fromWCharArray(error.message().c_str()), hresultText(error.code()));
}

} // namespace

class WindowsGraphicsCapture::Impl final {
public:
    explicit Impl(WindowsGraphicsCapture* owner) : owner_(owner) {}

    ~Impl() { stop(); }

    bool prepare(const Config& config, QString* error) {
        stop();
        if (config.monitorGeometry.width() <= 0 || config.monitorGeometry.height() <= 0 ||
            config.captureGeometry.width() <= 0 || config.captureGeometry.height() <= 0 ||
            config.fps < 1 || config.fps > 360 ||
            !config.monitorGeometry.contains(config.captureGeometry.topLeft()) ||
            !config.monitorGeometry.contains(config.captureGeometry.bottomRight())) {
            setError(error, QStringLiteral("Invalid Windows Graphics Capture geometry or frame rate"));
            return false;
        }
        config_ = config;
        width_ = config.captureGeometry.width();
        height_ = config.captureGeometry.height();
        frameBytes_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U);
        pipePath_ = QStringLiteral("\\\\.\\pipe\\LastFrameWgc_%1")
                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        pipe_ = CreateNamedPipeW(reinterpret_cast<LPCWSTR>(pipePath_.utf16()),
                                 PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1, 4U * 1024U * 1024U, 4U * 1024U * 1024U, 0, nullptr);
        if (pipe_ == INVALID_HANDLE_VALUE) {
            pipe_ = nullptr;
            setError(error, win32Text(GetLastError()));
            return false;
        }
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        frameEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stopEvent_ == nullptr || frameEvent_ == nullptr) {
            setError(error, win32Text(GetLastError()));
            closeHandles();
            return false;
        }
        prepared_ = true;
        haveFrame_ = false;
        captureError_ = false;
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
        closeHandles();
        pipePath_.clear();
        frameBytes_.clear();
        haveFrame_ = false;
        captureError_ = false;
    }

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] QString inputPath() const { return pipePath_; }
    [[nodiscard]] QSize frameSize() const noexcept { return QSize(width_, height_); }

private:
    static void setError(QString* error, const QString& value) {
        if (error != nullptr) {
            *error = value;
        }
    }

    void closeHandles() {
        if (pipe_ != nullptr && pipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_);
        }
        pipe_ = nullptr;
        if (stopEvent_ != nullptr) {
            CloseHandle(stopEvent_);
        }
        stopEvent_ = nullptr;
        if (frameEvent_ != nullptr) {
            CloseHandle(frameEvent_);
        }
        frameEvent_ = nullptr;
    }

    void fail(const QString& reason) {
        if (stopEvent_ != nullptr && WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0) {
            return;
        }
        captureError_ = true;
        emit owner_->failed(reason);
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
        const bool complete = connectError == ERROR_PIPE_CONNECTED ||
                              GetOverlappedResult(pipe_, &overlapped, &transferred, FALSE) != FALSE;
        const DWORD resultError = complete ? ERROR_SUCCESS : GetLastError();
        CloseHandle(overlapped.hEvent);
        if (!complete) {
            fail(win32Text(resultError));
            return false;
        }
        return true;
    }

    bool writeFrame(const std::vector<std::uint8_t>& frame) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr) {
            fail(win32Text(GetLastError()));
            return false;
        }
        const BOOL written = WriteFile(pipe_, frame.data(), static_cast<DWORD>(frame.size()), nullptr, &overlapped);
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
                              transferred == frame.size();
        const DWORD resultError = complete ? ERROR_SUCCESS : GetLastError();
        CloseHandle(overlapped.hEvent);
        if (!complete && resultError != ERROR_OPERATION_ABORTED) {
            fail(win32Text(resultError));
        }
        return complete;
    }

    bool copyNextFrame(const Direct3D11CaptureFramePool& pool,
                       ID3D11DeviceContext* context,
                       ID3D11Device* device) {
        auto frame = pool.TryGetNextFrame();
        if (!frame) {
            return false;
        }
        const auto contentSize = frame.ContentSize();
        if (contentSize.Width <= 0 || contentSize.Height <= 0) {
            return false;
        }
        const auto surface = frame.Surface();
        auto* inspectable = reinterpret_cast<IInspectable*>(winrt::get_abi(surface));
        ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
        HRESULT result = inspectable->QueryInterface(IID_PPV_ARGS(&access));
        if (FAILED(result)) {
            fail(QStringLiteral("WGC surface does not expose DXGI access: %1").arg(hresultText(result)));
            return false;
        }
        ComPtr<ID3D11Texture2D> source;
        result = access->GetInterface(IID_PPV_ARGS(&source));
        if (FAILED(result) || source == nullptr) {
            fail(QStringLiteral("WGC surface texture is unavailable: %1").arg(hresultText(result)));
            return false;
        }
        D3D11_TEXTURE2D_DESC sourceDescription{};
        source->GetDesc(&sourceDescription);
        if (sourceDescription.Width < static_cast<UINT>(config_.monitorGeometry.width()) ||
            sourceDescription.Height < static_cast<UINT>(config_.monitorGeometry.height())) {
            fail(QStringLiteral("WGC frame size is smaller than the selected monitor"));
            return false;
        }
        if (staging_ == nullptr || stagingWidth_ != sourceDescription.Width || stagingHeight_ != sourceDescription.Height ||
            stagingFormat_ != sourceDescription.Format) {
            D3D11_TEXTURE2D_DESC stagingDescription = sourceDescription;
            stagingDescription.MipLevels = 1;
            stagingDescription.ArraySize = 1;
            stagingDescription.Usage = D3D11_USAGE_STAGING;
            stagingDescription.BindFlags = 0;
            stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagingDescription.MiscFlags = 0;
            result = device->CreateTexture2D(&stagingDescription, nullptr, &staging_);
            if (FAILED(result)) {
                fail(QStringLiteral("WGC staging texture creation failed: %1").arg(hresultText(result)));
                return false;
            }
            stagingWidth_ = sourceDescription.Width;
            stagingHeight_ = sourceDescription.Height;
            stagingFormat_ = sourceDescription.Format;
        }
        context->CopyResource(staging_.Get(), source.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        result = context->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(result)) {
            fail(QStringLiteral("WGC staging texture map failed: %1").arg(hresultText(result)));
            return false;
        }
        const int offsetX = config_.captureGeometry.x() - config_.monitorGeometry.x();
        const int offsetY = config_.captureGeometry.y() - config_.monitorGeometry.y();
        const auto* sourceStart = static_cast<const std::uint8_t*>(mapped.pData);
        for (int row = 0; row < height_; ++row) {
            const auto* sourceRow = sourceStart + static_cast<std::size_t>(offsetY + row) * mapped.RowPitch +
                                    static_cast<std::size_t>(offsetX) * 4U;
            auto* destinationRow = frameBytes_.data() + static_cast<std::size_t>(row) *
                                   static_cast<std::size_t>(width_) * 4U;
            std::memcpy(destinationRow, sourceRow, static_cast<std::size_t>(width_) * 4U);
        }
        context->Unmap(staging_.Get(), 0);
        haveFrame_ = true;
        return true;
    }

    void captureLoop() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            if (!connectPipe()) {
                winrt::uninit_apartment();
                return;
            }
            HMONITOR monitor = MonitorFromPoint(POINT{config_.monitorGeometry.x() + 1,
                                                       config_.monitorGeometry.y() + 1},
                                                MONITOR_DEFAULTTONULL);
            if (monitor == nullptr) {
                fail(QStringLiteral("WGC monitor handle is unavailable"));
                winrt::uninit_apartment();
                return;
            }
            ComPtr<ID3D11Device> device;
            ComPtr<ID3D11DeviceContext> context;
            D3D_FEATURE_LEVEL featureLevel{};
            HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                                D3D11_SDK_VERSION, &device, &featureLevel, &context);
            if (FAILED(result)) {
                fail(QStringLiteral("WGC D3D11 device creation failed: %1").arg(hresultText(result)));
                winrt::uninit_apartment();
                return;
            }
            ComPtr<IDXGIDevice> dxgiDevice;
            result = device.As(&dxgiDevice);
            if (FAILED(result)) {
                fail(QStringLiteral("WGC DXGI device interface is unavailable: %1").arg(hresultText(result)));
                winrt::uninit_apartment();
                return;
            }
            winrt::com_ptr<IInspectable> inspectableDevice;
            winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectableDevice.put()));
            auto graphicsDevice = inspectableDevice.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

            auto factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            GraphicsCaptureItem item{nullptr};
            winrt::check_hresult(factory->CreateForMonitor(monitor, winrt::guid_of<GraphicsCaptureItem>(),
                                                            winrt::put_abi(item)));
            const auto size = item.Size();
            if (size.Width < config_.monitorGeometry.width() || size.Height < config_.monitorGeometry.height()) {
                fail(QStringLiteral("WGC capture item size is smaller than the selected monitor"));
                winrt::uninit_apartment();
                return;
            }
            auto framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
                graphicsDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
            auto session = framePool.CreateCaptureSession(item);
            session.IsCursorCaptureEnabled(config_.showCursor);
            const auto frameToken = framePool.FrameArrived(
                [this](const Direct3D11CaptureFramePool&, const winrt::Windows::Foundation::IInspectable&) {
                    SetEvent(frameEvent_);
                });
            session.StartCapture();

            // Keep the pacing period at sub-millisecond precision. Truncating
            // 60 FPS to 16 ms makes the producer run at 62.5 Hz and creates
            // irregular timestamps once FFmpeg segments the stream.
            const auto frameDuration = std::chrono::microseconds(
                1'000'000 / std::max(1, config_.fps));
            const DWORD waitMilliseconds = static_cast<DWORD>(
                std::max<std::int64_t>(1, (frameDuration.count() + 999) / 1000));
            auto nextFrame = std::chrono::steady_clock::now();
            while (WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0 && !captureError_) {
                const HANDLE handles[] = {frameEvent_, stopEvent_};
                const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE,
                                                                 waitMilliseconds);
                if (waitResult == WAIT_OBJECT_0 + 1) {
                    break;
                }
                ResetEvent(frameEvent_);
                while (WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0 && !captureError_ &&
                       copyNextFrame(framePool, context.Get(), device.Get())) {
                }
                if (!haveFrame_) {
                    continue;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now >= nextFrame && !writeFrame(frameBytes_)) {
                    break;
                }
                nextFrame += frameDuration;
                if (std::chrono::steady_clock::now() > nextFrame + frameDuration * 2) {
                    nextFrame = std::chrono::steady_clock::now();
                }
            }
            framePool.FrameArrived(frameToken);
            session = nullptr;
            framePool = nullptr;
            winrt::uninit_apartment();
        } catch (const winrt::hresult_error& error) {
            fail(QStringLiteral("Windows Graphics Capture failed: %1").arg(winrtText(error)));
            try {
                winrt::uninit_apartment();
            } catch (...) {
            }
        } catch (...) {
            fail(QStringLiteral("Windows Graphics Capture failed with an unknown error"));
            try {
                winrt::uninit_apartment();
            } catch (...) {
            }
        }
    }

    WindowsGraphicsCapture* owner_ = nullptr;
    Config config_;
    HANDLE pipe_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    HANDLE frameEvent_ = nullptr;
    QString pipePath_;
    std::thread worker_;
    std::vector<std::uint8_t> frameBytes_;
    int width_ = 0;
    int height_ = 0;
    bool prepared_ = false;
    bool running_ = false;
    bool haveFrame_ = false;
    bool captureError_ = false;
    ComPtr<ID3D11Texture2D> staging_;
    UINT stagingWidth_ = 0;
    UINT stagingHeight_ = 0;
    DXGI_FORMAT stagingFormat_ = DXGI_FORMAT_UNKNOWN;
};

WindowsGraphicsCapture::WindowsGraphicsCapture(QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(this)) {}

WindowsGraphicsCapture::~WindowsGraphicsCapture() = default;

bool WindowsGraphicsCapture::prepare(const Config& config, QString* error) {
    return impl_->prepare(config, error);
}

bool WindowsGraphicsCapture::start() {
    return impl_->start();
}

void WindowsGraphicsCapture::stop() {
    impl_->stop();
}

bool WindowsGraphicsCapture::isPrepared() const noexcept {
    return impl_->isPrepared();
}

QString WindowsGraphicsCapture::inputPath() const {
    return impl_->inputPath();
}

QSize WindowsGraphicsCapture::frameSize() const noexcept {
    return impl_->frameSize();
}

} // namespace LastFrame::Platform
