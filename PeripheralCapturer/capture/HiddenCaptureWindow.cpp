#include "HiddenCaptureWindow.h"

#include <spdlog/spdlog.h>

namespace {

constexpr wchar_t kClassName[] = L"PC_HiddenCapture";

LRESULT CALLBACK captureWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INPUT:
        // 热路径：拷包入队
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

bool HiddenCaptureWindow::create() {
    if (hwnd_) {
        spdlog::warn("[capture] create() called twice");
        return true;
    }

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
    spdlog::info("[capture] init hwnd={}", static_cast<void*>(hwnd_));
    return true;
}

void HiddenCaptureWindow::destroy() {
    if (!hwnd_) {
        return;
    }
    if (!DestroyWindow(hwnd_)) {
        spdlog::warn("[capture] DestroyWindow failed err={}", GetLastError());
    } else {
        spdlog::info("[capture] destroyed");
    }
    hwnd_ = nullptr;
}
