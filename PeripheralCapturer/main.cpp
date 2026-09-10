#include "Layout/MainWindow.h"
#include "utils/Logger.h"
#include "Input/Timer.h"

#include <QApplication>

#include <spdlog/spdlog.h>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    initLogger();
    spdlog::info("[app] starting");
    Timer::init();

    MainWindow config;
    config.resize(1280, 720);
    config.show();

    const int code = app.exec();
    spdlog::info("[app] exiting code={}", code);
    shutdownLogger();
    return code;
}
