#include "platform/WindowsDesktopCapture.h"

#if !defined(Q_OS_WIN)
#error "WindowsDesktopCapture is a Windows-only backend"
#endif

#include <QUuid>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

using Microsoft::WRL::ComPtr;

namespace LastFrame::Platform {

namespace {

[[nodiscard]] QString hresultText(const HRESULT result) {
    return QStringLiteral("0x%1").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}

[[nodiscard]] QString win32Text(const DWORD error) {
    return QStringLiteral("Win32 error %1").arg(error);
}

} // namespace

class WindowsDesktopCapture::Impl final {
public:
    explicit Impl(WindowsDesktopCapture* owner) : owner_(owner) {}

    ~Impl() { stop(); }

    bool prepare(const Config& config, QString* error) {
        stop();
        if (config.captureGeometry.width() <= 0 || config.captureGeometry.height() <= 0 ||
            config.fps < 1 || config.fps > 360) {
            setError(error, QStringLiteral("Invalid native capture geometry or frame rate"));
            return false;
        }
        config_ = config;
        width_ = config.captureGeometry.width();
        height_ = config.captureGeometry.height();
        frameBytes_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U);
        baseFrameBytes_.resize(frameBytes_.size());

        pipePath_ = QStringLiteral("\\\\.\\pipe\\LastFrame_%1")
                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        pipe_ = CreateNamedPipeW(reinterpret_cast<LPCWSTR>(pipePath_.utf16()),
                                 PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1,
                                 4U * 1024U * 1024U,
                                 4U * 1024U * 1024U,
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
        haveFrame_ = false;
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
        frameBytes_.clear();
        baseFrameBytes_.clear();
        haveFrame_ = false;
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

    bool connectPipe() {
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr) {
            fail(win32Text(GetLastError()));
            return false;
        }
        const BOOL connected = ConnectNamedPipe(pipe_, &overlapped);
        if (!connected && GetLastError() != ERROR_IO_PENDING && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(overlapped.hEvent);
            fail(win32Text(GetLastError()));
            return false;
        }
        if (!connected && GetLastError() == ERROR_PIPE_CONNECTED) {
            SetEvent(overlapped.hEvent);
        }
        const HANDLE handles[] = {overlapped.hEvent, stopEvent_};
        const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        const bool stopped = waitResult == WAIT_OBJECT_0 + 1;
        if (stopped) {
            CancelIoEx(pipe_, &overlapped);
        }
        if (stopped) {
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
        if (!complete &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            fail(win32Text(GetLastError()));
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
        if (!written && GetLastError() != ERROR_IO_PENDING) {
            CloseHandle(overlapped.hEvent);
            if (GetLastError() != ERROR_OPERATION_ABORTED) {
                fail(win32Text(GetLastError()));
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
        if (!complete && GetLastError() != ERROR_OPERATION_ABORTED) {
            fail(win32Text(GetLastError()));
        }
        CloseHandle(overlapped.hEvent);
        return complete;
    }

    bool initializeCursorDrawer() {
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = width_;
        bitmapInfo.bmiHeader.biHeight = -height_;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        cursorDc_ = CreateCompatibleDC(nullptr);
        if (cursorDc_ == nullptr) {
            return false;
        }
        cursorBitmap_ = CreateDIBSection(cursorDc_, &bitmapInfo, DIB_RGB_COLORS, &cursorBits_, nullptr, 0);
        if (cursorBitmap_ == nullptr || cursorBits_ == nullptr) {
            destroyCursorDrawer();
            return false;
        }
        SelectObject(cursorDc_, cursorBitmap_);
        return true;
    }

    void destroyCursorDrawer() {
        if (cursorDc_ != nullptr) {
            DeleteDC(cursorDc_);
        }
        cursorDc_ = nullptr;
        if (cursorBitmap_ != nullptr) {
            DeleteObject(cursorBitmap_);
        }
        cursorBitmap_ = nullptr;
        cursorBits_ = nullptr;
    }

    void drawCursor(std::vector<std::uint8_t>& frame) {
        if (!config_.showCursor || cursorDc_ == nullptr || cursorBits_ == nullptr) {
            return;
        }
        CURSORINFO cursorInfo{sizeof(CURSORINFO)};
        if (!GetCursorInfo(&cursorInfo) || (cursorInfo.flags & CURSOR_SHOWING) == 0) {
            return;
        }
        ICONINFO iconInfo{};
        if (!GetIconInfo(cursorInfo.hCursor, &iconInfo)) {
            return;
        }
        const int x = cursorInfo.ptScreenPos.x - config_.captureGeometry.x() - static_cast<int>(iconInfo.xHotspot);
        const int y = cursorInfo.ptScreenPos.y - config_.captureGeometry.y() - static_cast<int>(iconInfo.yHotspot);
        std::memcpy(cursorBits_, frame.data(), frame.size());
        DrawIconEx(cursorDc_, x, y, cursorInfo.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
        std::memcpy(frame.data(), cursorBits_, frame.size());
        if (iconInfo.hbmMask != nullptr) {
            DeleteObject(iconInfo.hbmMask);
        }
        if (iconInfo.hbmColor != nullptr) {
            DeleteObject(iconInfo.hbmColor);
        }
    }

    bool captureFrame(IDXGIOutputDuplication* duplication,
                      ID3D11DeviceContext* context,
                      ID3D11Texture2D* staging,
                      std::vector<std::uint8_t>& frame) {
        DXGI_OUTDUPL_FRAME_INFO frameInfo{};
        ComPtr<IDXGIResource> resource;
        // Desktop Duplication only reports new frames when the desktop changes.
        // Poll briefly and repeat the last frame on the requested master cadence
        // so a static desktop still produces a constant-FPS video stream.
        const HRESULT acquired = duplication->AcquireNextFrame(0, &frameInfo, &resource);
        if (acquired == DXGI_ERROR_WAIT_TIMEOUT) {
            if (!haveFrame_) {
                return true;
            }
            if (config_.showCursor) {
                std::memcpy(frame.data(), baseFrameBytes_.data(), frame.size());
                drawCursor(frame);
            }
            return writeFrame(frame);
        }
        if (FAILED(acquired)) {
            fail(QStringLiteral("DXGI AcquireNextFrame failed: %1").arg(hresultText(acquired)));
            return false;
        }
        const auto releaseFrame = [&] { duplication->ReleaseFrame(); };
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(resource.As(&texture))) {
            releaseFrame();
            fail(QStringLiteral("DXGI frame is not a D3D11 texture"));
            return false;
        }
        context->CopyResource(staging, texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT mappedResult = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(mappedResult)) {
            releaseFrame();
            fail(QStringLiteral("D3D11 staging texture map failed: %1").arg(hresultText(mappedResult)));
            return false;
        }
        const auto* outputStart = static_cast<const std::uint8_t*>(mapped.pData);
        const DXGI_OUTPUT_DESC outputDesc = outputDescription_;
        const int localX = config_.captureGeometry.x() - outputDesc.DesktopCoordinates.left;
        const int localY = config_.captureGeometry.y() - outputDesc.DesktopCoordinates.top;
        for (int row = 0; row < height_; ++row) {
            const auto* source = outputStart + static_cast<std::size_t>(localY + row) * mapped.RowPitch +
                                 static_cast<std::size_t>(localX) * 4U;
            auto* destination = frame.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(width_) * 4U;
            std::memcpy(destination, source, static_cast<std::size_t>(width_) * 4U);
        }
        context->Unmap(staging, 0);
        releaseFrame();
        std::memcpy(baseFrameBytes_.data(), frame.data(), frame.size());
        drawCursor(frame);
        haveFrame_ = true;
        return writeFrame(frame);
    }

    void captureLoop() {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
            fail(QStringLiteral("COM initialization failed: %1").arg(hresultText(comResult)));
            return;
        }

        if (!connectPipe()) {
            if (SUCCEEDED(comResult)) {
                CoUninitialize();
            }
            return;
        }

        ComPtr<IDXGIFactory1> factory;
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<IDXGIOutput> output;
        ComPtr<IDXGIOutput1> output1;
        ComPtr<IDXGIOutputDuplication> duplication;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D11Texture2D> staging;
        D3D_FEATURE_LEVEL featureLevel{};

        HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(result)) {
            fail(QStringLiteral("DXGI factory creation failed: %1").arg(hresultText(result)));
            cleanupCom(comResult);
            return;
        }
        bool outputFound = false;
        for (UINT adapterIndex = 0; !outputFound; ++adapterIndex) {
            ComPtr<IDXGIAdapter1> candidateAdapter;
            if (factory->EnumAdapters1(adapterIndex, &candidateAdapter) == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (candidateAdapter == nullptr) {
                continue;
            }
            for (UINT outputIndex = 0; ; ++outputIndex) {
                ComPtr<IDXGIOutput> candidateOutput;
                if (candidateAdapter->EnumOutputs(outputIndex, &candidateOutput) == DXGI_ERROR_NOT_FOUND) {
                    break;
                }
                if (candidateOutput == nullptr) {
                    continue;
                }
                DXGI_OUTPUT_DESC candidateDescription{};
                if (FAILED(candidateOutput->GetDesc(&candidateDescription))) {
                    continue;
                }
                const QRect candidateGeometry(candidateDescription.DesktopCoordinates.left,
                                               candidateDescription.DesktopCoordinates.top,
                                               candidateDescription.DesktopCoordinates.right -
                                                   candidateDescription.DesktopCoordinates.left,
                                               candidateDescription.DesktopCoordinates.bottom -
                                                   candidateDescription.DesktopCoordinates.top);
                if (candidateGeometry == config_.monitorGeometry) {
                    adapter = candidateAdapter;
                    output = candidateOutput;
                    outputDescription_ = candidateDescription;
                    outputFound = true;
                    break;
                }
            }
        }
        if (!outputFound) {
            ComPtr<IDXGIAdapter1> fallbackAdapter;
            ComPtr<IDXGIOutput> fallbackOutput;
            if (SUCCEEDED(factory->EnumAdapters1(0, &fallbackAdapter)) && fallbackAdapter != nullptr &&
                SUCCEEDED(fallbackAdapter->EnumOutputs(static_cast<UINT>(config_.outputIndex), &fallbackOutput)) &&
                fallbackOutput != nullptr && SUCCEEDED(fallbackOutput->GetDesc(&outputDescription_))) {
                adapter = fallbackAdapter;
                output = fallbackOutput;
                outputFound = true;
            }
        }
        if (!outputFound) {
            fail(QStringLiteral("DXGI output matching the selected monitor was not found"));
            cleanupCom(comResult);
            return;
        }
        const QRect outputRect(outputDescription_.DesktopCoordinates.left,
                               outputDescription_.DesktopCoordinates.top,
                               outputDescription_.DesktopCoordinates.right - outputDescription_.DesktopCoordinates.left,
                               outputDescription_.DesktopCoordinates.bottom - outputDescription_.DesktopCoordinates.top);
        const QRect localCapture = config_.captureGeometry.translated(-outputRect.topLeft());
        if (!outputRect.contains(config_.captureGeometry.topLeft()) ||
            !outputRect.contains(config_.captureGeometry.bottomRight()) || localCapture.x() < 0 ||
            localCapture.y() < 0 || localCapture.right() >= outputRect.width() ||
            localCapture.bottom() >= outputRect.height()) {
            fail(QStringLiteral("Requested capture area is outside the DXGI output"));
            cleanupCom(comResult);
            return;
        }
        result = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                   D3D11_SDK_VERSION, &device, &featureLevel, &context);
        if (FAILED(result)) {
            fail(QStringLiteral("D3D11 device creation failed: %1").arg(hresultText(result)));
            cleanupCom(comResult);
            return;
        }
        result = output.As(&output1);
        if (FAILED(result)) {
            fail(QStringLiteral("DXGI output duplication interface unavailable"));
            cleanupCom(comResult);
            return;
        }
        result = output1->DuplicateOutput(device.Get(), &duplication);
        if (FAILED(result)) {
            fail(QStringLiteral("DXGI Desktop Duplication unavailable: %1").arg(hresultText(result)));
            cleanupCom(comResult);
            return;
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(outputRect.width());
        description.Height = static_cast<UINT>(outputRect.height());
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_STAGING;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        result = device->CreateTexture2D(&description, nullptr, &staging);
        if (FAILED(result)) {
            fail(QStringLiteral("D3D11 staging texture creation failed: %1").arg(hresultText(result)));
            cleanupCom(comResult);
            return;
        }
        if (config_.showCursor && !initializeCursorDrawer()) {
            fail(QStringLiteral("Cursor overlay initialization failed"));
            cleanupCom(comResult);
            return;
        }

        const auto frameDuration = std::chrono::microseconds(1'000'000 / std::max(1, config_.fps));
        auto nextFrame = std::chrono::steady_clock::now();
        while (WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0) {
            if (!captureFrame(duplication.Get(), context.Get(), staging.Get(), frameBytes_)) {
                break;
            }
            nextFrame += frameDuration;
            std::this_thread::sleep_until(nextFrame);
            if (std::chrono::steady_clock::now() > nextFrame + frameDuration * 2) {
                nextFrame = std::chrono::steady_clock::now();
            }
        }
        destroyCursorDrawer();
        cleanupCom(comResult);
    }

    void cleanupCom(const HRESULT comResult) {
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
    }

    WindowsDesktopCapture* owner_ = nullptr;
    Config config_;
    HANDLE pipe_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    QString pipePath_;
    std::thread worker_;
    std::vector<std::uint8_t> frameBytes_;
    std::vector<std::uint8_t> baseFrameBytes_;
    int width_ = 0;
    int height_ = 0;
    bool prepared_ = false;
    bool running_ = false;
    bool haveFrame_ = false;
    DXGI_OUTPUT_DESC outputDescription_{};
    HDC cursorDc_ = nullptr;
    HBITMAP cursorBitmap_ = nullptr;
    void* cursorBits_ = nullptr;
};

WindowsDesktopCapture::WindowsDesktopCapture(QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(this)) {}

WindowsDesktopCapture::~WindowsDesktopCapture() = default;

bool WindowsDesktopCapture::prepare(const Config& config, QString* error) {
    return impl_->prepare(config, error);
}

bool WindowsDesktopCapture::start() {
    return impl_->start();
}

void WindowsDesktopCapture::stop() {
    impl_->stop();
}

bool WindowsDesktopCapture::isPrepared() const noexcept {
    return impl_->isPrepared();
}

QString WindowsDesktopCapture::inputPath() const {
    return impl_->inputPath();
}

QSize WindowsDesktopCapture::frameSize() const noexcept {
    return impl_->frameSize();
}

} // namespace LastFrame::Platform
