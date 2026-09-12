#include "HiddenCaptureWindow.h"

#include "../Input/Timer.h"

#include <spdlog/spdlog.h>

#include <vector>

namespace {

constexpr wchar_t kClassName[] = L"PC_HiddenCapture";

HiddenCaptureWindow* selfFrom(HWND hwnd) {
    return reinterpret_cast<HiddenCaptureWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

} // namespace

LRESULT CALLBACK captureWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INPUT: {
        // 热路径：拷包 + 时间戳 + tryPush，立刻返回。禁止解析 / 写盘 / spdlog。
        if (auto* self = selfFrom(hwnd)) {
            self->enqueueInput(lParam);
        }
        return 0;
    }
    case WM_INPUT_DEVICE_CHANGE: {
        if (auto* self = selfFrom(hwnd)) {
            self->enqueueDeviceChange(wParam, lParam);
        }
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void HiddenCaptureWindow::enqueueInput(LPARAM lParam) {
    if (!packets_) {
        return;
    }
    UINT size = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size,
                    sizeof(RAWINPUTHEADER));
    if (size == 0) {
        return;
    }
    RawInputPacket pkt;
    pkt.kind = RawPacketKind::Input;
    pkt.timestampUs = Timer::nowUs();
    pkt.bytes.resize(size);
    UINT written = size;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, pkt.bytes.data(),
                        &written, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) ||
        written < sizeof(RAWINPUTHEADER)) {
        return;
    }
    pkt.bytes.resize(written);
    const auto* header = reinterpret_cast<const RAWINPUTHEADER*>(pkt.bytes.data());
    pkt.hDevice = header->hDevice;
    pkt.dwType = header->dwType;
    packets_->tryPush(std::move(pkt));
}

void HiddenCaptureWindow::enqueueDeviceChange(WPARAM wParam, LPARAM lParam) {
    if (!packets_) {
        return;
    }
    RawInputPacket pkt;
    pkt.kind = (wParam == GIDC_REMOVAL) ? RawPacketKind::DeviceRemoval
                                        : RawPacketKind::DeviceArrival;
    pkt.timestampUs = Timer::nowUs();
    pkt.hDevice = reinterpret_cast<HANDLE>(lParam);
    packets_->tryPush(std::move(pkt));
}

bool HiddenCaptureWindow::create(BoundedQueue<RawInputPacket>& packets) {
    if (hwnd_) {
        spdlog::warn("[capture] create() called twice");
        return true;
    }
    packets_ = &packets;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!instance) {
        spdlog::critical("[capture] GetModuleHandleW failed err={}", GetLastError());
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = captureWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        spdlog::error("[capture] RegisterClassEx failed err={}", GetLastError());
        return false;
    }

    hwnd_ = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                            instance, nullptr);
    if (!hwnd_) {
        spdlog::critical("[capture] CreateWindowEx HWND_MESSAGE failed err={}", GetLastError());
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    RAWINPUTDEVICE rid[2]{};
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x06; // keyboard
    rid[0].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    rid[0].hwndTarget = hwnd_;
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x02; // mouse
    rid[1].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    rid[1].hwndTarget = hwnd_;
    if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE))) {
        spdlog::critical("[capture] RegisterRawInputDevices failed err={}", GetLastError());
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        packets_ = nullptr;
        return false;
    }
    registered_ = true;
    spdlog::info("[capture] init hwnd={} raw=keyboard+mouse sink", static_cast<void*>(hwnd_));
    return true;
}

void HiddenCaptureWindow::destroy() {
    if (registered_) {
        RAWINPUTDEVICE rid[2]{};
        rid[0].usUsagePage = 0x01;
        rid[0].usUsage = 0x06;
        rid[0].dwFlags = RIDEV_REMOVE;
        rid[1].usUsagePage = 0x01;
        rid[1].usUsage = 0x02;
        rid[1].dwFlags = RIDEV_REMOVE;
        if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE))) {
            spdlog::warn("[capture] unregister Raw Input failed err={}", GetLastError());
        }
        registered_ = false;
    }
    if (!hwnd_) {
        packets_ = nullptr;
        return;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
    if (!DestroyWindow(hwnd_)) {
        spdlog::warn("[capture] DestroyWindow failed err={}", GetLastError());
    } else {
        spdlog::info("[capture] destroyed");
    }
    hwnd_ = nullptr;
    packets_ = nullptr;
}
