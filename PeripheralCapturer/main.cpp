#include "Layout/MainWindow.h"
#include "overlay/PovWindow.h"
#include "capture/HiddenCaptureWindow.h"
#include "utils/Logger.h"
#include "Input/Timer.h"

#include <QApplication>
#include <QtWebView/qtwebviewfunctions.h>

#include <spdlog/spdlog.h>

int main(int argc, char* argv[]) {
    QtWebView::initialize();
    QApplication app(argc, argv);
    initLogger();
    spdlog::info("[app] init starting");
    Timer::init();

    // 三扇窗：捕获（不可见）→ 配置 → POV，互不嵌套。
    HiddenCaptureWindow capture;
    if (!capture.create()) {
        spdlog::critical("[app] capture window init failed, abort");
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
    if (code != 0) {
        spdlog::error("[app] exiting code={}", code);
    } else {
        spdlog::info("[app] exiting code=0");
    }
    shutdownLogger();
    return code;
}
