#include "Layout/MainWindow.h"
#include "overlay/PovWindow.h"
#include "capture/HiddenCaptureWindow.h"
#include "utils/Logger.h"
#include "Input/Timer.h"
#include "storage/Database.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QtWebView/qtwebviewfunctions.h>

#include <spdlog/spdlog.h>

int main(int argc, char* argv[]) {
    // 必须在创建 WebView2 控制器之前：A=00 全透明，否则浮层是不透明黑/白底。
    qputenv("WEBVIEW2_DEFAULT_BACKGROUND_COLOR", QByteArrayLiteral("00FFFFFF"));
    QtWebView::initialize();
    QApplication app(argc, argv);
    initLogger();
    spdlog::info("[app] init starting");
    spdlog::debug("[pov] env WEBVIEW2_DEFAULT_BACKGROUND_COLOR={}",
                  qgetenv("WEBVIEW2_DEFAULT_BACKGROUND_COLOR").constData());
    Timer::init();

    Database database;
    const QString dbPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data.db"));
    if (!database.open(dbPath)) {
        spdlog::critical("[app] database init failed, abort");
        shutdownLogger();
        return 1;
    }

    // 三扇窗：捕获（不可见）→ 配置 → POV，互不嵌套。
    HiddenCaptureWindow capture;
    if (!capture.create()) {
        spdlog::critical("[app] capture window init failed, abort");
        database.close();
        shutdownLogger();
        return 1;
    }

    MainWindow config;
    config.resize(1280, 720);
    config.show();
    spdlog::info("[app] config window shown");

    PovWindow pov;
    pov.show();
    pov.setClickThrough(true);
    spdlog::info("[app] pov window shown");

    const int code = app.exec();

    capture.destroy();
    database.close();
    if (code != 0) {
        spdlog::error("[app] exiting code={}", code);
    } else {
        spdlog::info("[app] exiting code=0");
    }
    shutdownLogger();
    return code;
}
