#include "PovWindow.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dwmapi.h>

#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <utility>
#include <QtWebView/QWebViewLoadingInfo>

#include <spdlog/spdlog.h>

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

namespace {

constexpr COLORREF kFrameColor = RGB(0, 229, 255);

const char* loadStatusName(QWebViewLoadingInfo::LoadStatus status) {
    switch (status) {
    case QWebViewLoadingInfo::LoadStatus::Started:
        return "Started";
    case QWebViewLoadingInfo::LoadStatus::Stopped:
        return "Stopped";
    case QWebViewLoadingInfo::LoadStatus::Succeeded:
        return "Succeeded";
    case QWebViewLoadingInfo::LoadStatus::Failed:
        return "Failed";
    }
    return "Unknown";
}

struct ChildDump {
    int count = 0;
};

BOOL CALLBACK dumpChild(HWND hwnd, LPARAM lParam) {
    auto* dump = reinterpret_cast<ChildDump*>(lParam);
    wchar_t cls[128] = {};
    GetClassNameW(hwnd, cls, 128);
    RECT rc{};
    GetWindowRect(hwnd, &rc);
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    spdlog::debug("[pov]   child#{} hwnd={} class={} vis={} {}x{} ex={:#x}", dump->count,
                  static_cast<void*>(hwnd), QString::fromWCharArray(cls).toStdString(),
                  IsWindowVisible(hwnd) != 0, rc.right - rc.left, rc.bottom - rc.top,
                  static_cast<unsigned long long>(ex));
    ++dump->count;
    EnumChildWindows(hwnd, dumpChild, lParam);
    return TRUE;
}

bool isWebView2Compositor(HWND hwnd) {
    wchar_t cls[128] = {};
    if (GetClassNameW(hwnd, cls, 128) <= 0) {
        return false;
    }
    // 合成窗打 layered / color-key 会把 WebView2 打崩（读 0xFFFFFFFFFFFFFFFF）。
    return wcsstr(cls, L"Chrome_") != nullptr || wcsstr(cls, L"Intermediate D3D") != nullptr;
}

void applyHitTestStyle(HWND hwnd, bool clickThrough, const char* tag, bool layeredColorKey) {
    if (!hwnd || !IsWindow(hwnd)) {
        spdlog::debug("[pov] applyHitTestStyle {} hwnd invalid", tag);
        return;
    }
    if (isWebView2Compositor(hwnd)) {
        return;
    }
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    if (layeredColorKey) {
        ex |= WS_EX_LAYERED;
    }
    if (clickThrough) {
        ex |= WS_EX_TRANSPARENT;
    } else {
        ex &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    if (!layeredColorKey) {
        return;
    }
    SetLastError(0);
    if (!SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_COLORKEY)) {
        spdlog::warn("[pov] SetLayeredWindowAttributes failed tag={} err={}", tag, GetLastError());
    }
}

void applyDwmFrame(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }
    const HRESULT hr =
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &kFrameColor, sizeof(kFrameColor));
    if (FAILED(hr)) {
        spdlog::warn("[pov] DWMWA_BORDER_COLOR failed hr={:#x}", static_cast<unsigned>(hr));
    } else {
        spdlog::debug("[pov] DWMWA_BORDER_COLOR hwnd={} ok", static_cast<void*>(hwnd));
    }
}

BOOL CALLBACK applyHitTestToChild(HWND hwnd, LPARAM lParam) {
    // 子窗只加穿透，不加 color-key。递归 Enum 已由 applyClickThrough 做一层即可。
    applyHitTestStyle(hwnd, lParam != 0, "child", false);
    return TRUE;
}

} // namespace

void PovWindow::loadOfflineHtml() {
    if (loadedOfflineHtml_) {
        return;
    }
    QFile file(QStringLiteral(":/pov/index.html"));
    if (!file.open(QIODevice::ReadOnly)) {
        spdlog::error("[pov] cannot read qrc:/pov/index.html");
        return;
    }
    const QString html = QString::fromUtf8(file.readAll());
    loadedOfflineHtml_ = true;
    spdlog::info("[pov] loadHtml offline bytes={}", html.toUtf8().size());
    loadHtml(html);
}

void PovWindow::loadDevPage() {
    loadedOfflineHtml_ = false;
    QUrl page(QStringLiteral("http://127.0.0.1:5173/"));
    if (!nativeWsUrl_.isEmpty()) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("ws"), nativeWsUrl_);
        page.setQuery(query);
    }
    setUrl(page);
}

void PovWindow::setNativeWsUrl(QString url) {
    nativeWsUrl_ = std::move(url);
}

PovWindow::PovWindow(QWindow* parent): QWebView(parent) {
    setTitle(QStringLiteral("POV"));
    setFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint |
             Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);

#ifdef NDEBUG
    const char* buildKind = "release";
#else
    const char* buildKind = "debug";
#endif

    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        resize(400, 240);
        setPosition(area.right() - 420, area.top() + 48);
        spdlog::debug("[pov] place on screen avail={}x{} pos={},{}", area.width(), area.height(),
                      area.right() - 420, area.top() + 48);
    } else {
        resize(400, 240);
        spdlog::warn("[pov] primaryScreen is null, using default 400x240");
    }

    connect(this, &QWebView::loadingChanged, this, [this](const QWebViewLoadingInfo& info) {
        const auto status = info.status();
        if (status == QWebViewLoadingInfo::LoadStatus::Failed) {
            spdlog::warn("[pov] load failed url={} err={}", info.url().toString().toStdString(),
                         info.errorString().toStdString());
            // 失败回调里同步 loadHtml 会重入 WebView2，等事件转完再换页。
            QTimer::singleShot(0, this, [this] { loadOfflineHtml(); });
        } else if (status == QWebViewLoadingInfo::LoadStatus::Succeeded) {
            spdlog::info("[pov] load ok url={}", info.url().toString().toStdString());
        } else {
            spdlog::debug("[pov] loadingChanged status={} url={}", loadStatusName(status),
                          info.url().toString().toStdString());
        }
        applyClickThrough();
    });
    connect(this, &QWebView::loadProgressChanged, this, [this](int progress) {
        spdlog::debug("[pov] loadProgress={}", progress);
        if (progress >= 100) {
            applyClickThrough();
        }
    });

    // Debug：main 等 Vite 就绪且 WS 地址已知再 loadDevPage。

    spdlog::info("[pov] window created build={} clickThrough={}", buildKind, clickThrough_);
}

bool PovWindow::event(QEvent* event) {
    const bool result = QWebView::event(event);
    if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) {
        spdlog::debug("[pov] event type={} winId={}", static_cast<int>(event->type()),
                      static_cast<void*>(reinterpret_cast<HWND>(winId())));
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
    // show 之后不要 setFlags：QWindow 会重建 HWND，WebView2 控制器仍绑旧窗，读野指针崩。
    applyClickThrough();
}

void PovWindow::applyClickThrough() {
    static int calls = 0;
    ++calls;
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        spdlog::debug("[pov] applyClickThrough #{} skipped, winId is null", calls);
        return;
    }
    RECT wr{};
    GetWindowRect(hwnd, &wr);
    spdlog::debug("[pov] applyClickThrough #{} hwnd={} vis={} {}x{}", calls, static_cast<void*>(hwnd),
                  isVisible(), wr.right - wr.left, wr.bottom - wr.top);
    applyHitTestStyle(hwnd, clickThrough_, "root", true);
    applyDwmFrame(hwnd);
    if (calls <= 8) {
        ChildDump dump;
        EnumChildWindows(hwnd, dumpChild, reinterpret_cast<LPARAM>(&dump));
        spdlog::debug("[pov] childCount={}", dump.count);
    }
    EnumChildWindows(hwnd, applyHitTestToChild, clickThrough_ ? 1 : 0);
}
