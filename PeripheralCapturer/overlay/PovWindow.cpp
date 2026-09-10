#include "PovWindow.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QtWebView/QWebViewLoadingInfo>

#include <spdlog/spdlog.h>

namespace {

void applyHitTestStyle(HWND hwnd, bool clickThrough) {
    if (!hwnd) {
        return;
    }
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    if (clickThrough) {
        ex |= WS_EX_TRANSPARENT;
    } else {
        ex &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    if (!SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)) {
        static bool logged = false;
        if (!logged) {
            spdlog::warn("[pov] SetLayeredWindowAttributes failed err={}", GetLastError());
            logged = true;
        }
    }
}

BOOL CALLBACK applyHitTestToChild(HWND hwnd, LPARAM lParam) {
    applyHitTestStyle(hwnd, lParam != 0);
    return TRUE;
}

} // namespace

PovWindow::PovWindow(QWindow* parent): QWebView(parent) {
    setTitle(QStringLiteral("POV"));
    setFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint |
             Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
#ifdef NDEBUG
    setUrl(QUrl(QStringLiteral("http://127.0.0.1:5173/")));
#else
    setUrl(QUrl(QStringLiteral("qrc:/pov/index.html")));
#endif

    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        resize(400, 240);
        setPosition(area.right() - 420, area.top() + 48);
    } else {
        resize(400, 240);
    }

    connect(this, &QWebView::loadingChanged, this, [this](const QWebViewLoadingInfo& info) {
        using Status = QWebViewLoadingInfo::LoadStatus;
        if (info.status() == Status::Failed) {
            spdlog::error("[pov] load failed url={} err={}", info.url().toString().toStdString(),
                          info.errorString().toStdString());
        } else if (info.status() == Status::Succeeded) {
            spdlog::info("[pov] load ok url={}", info.url().toString().toStdString());
        }
        applyClickThrough();
    });
    connect(this, &QWebView::loadProgressChanged, this, [this](int progress) {
        if (progress >= 100) {
            applyClickThrough();
        }
    });

    spdlog::info("[pov] window created url={}", url().toString().toStdString());
}

bool PovWindow::event(QEvent* event) {
    const bool result = QWebView::event(event);
    if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) {
        applyClickThrough();
        QTimer::singleShot(0, this, [this] { applyClickThrough(); });
        QTimer::singleShot(200, this, [this] { applyClickThrough(); });
        QTimer::singleShot(800, this, [this] { applyClickThrough(); });
    }
    return result;
}

void PovWindow::setClickThrough(bool enabled) {
    clickThrough_ = enabled;
    spdlog::info("[pov] setClickThrough {}", enabled);
    auto flags = this->flags();
    if (enabled) {
        flags |= Qt::WindowTransparentForInput;
    } else {
        flags &= ~Qt::WindowTransparentForInput;
    }
    setFlags(flags);
    applyClickThrough();
}

void PovWindow::applyClickThrough() {
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        spdlog::debug("[pov] applyClickThrough skipped, winId is null");
        return;
    }
    applyHitTestStyle(hwnd, clickThrough_);
    EnumChildWindows(hwnd, applyHitTestToChild, clickThrough_ ? 1 : 0);
}
