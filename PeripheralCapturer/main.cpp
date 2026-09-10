#include "Layout/MainWindow.h"
#include "overlay/PovWindow.h"
#include "utils/Logger.h"
#include "Input/Timer.h"

#include <QApplication>
#include <QtWebView/qtwebviewfunctions.h>

#include <spdlog/spdlog.h>

int main(int argc, char* argv[]) {
    QtWebView::initialize();
    QApplication app(argc, argv);
    initLogger();
    spdlog::info("[app] starting");
    Timer::init();

    MainWindow config;
    config.resize(1280, 720);
    config.show();

    PovWindow pov;
    pov.show();
    pov.setClickThrough(true);

    const int code = app.exec();
    spdlog::info("[app] exiting code={}", code);
    shutdownLogger();
    return code;
}
