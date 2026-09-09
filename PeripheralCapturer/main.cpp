#include "Layout/MainWindow.h"
#include "utils/Logger.h"
#include "Input/Timer.h"
#include <QApplication>

#include <spdlog/spdlog.h>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    initLogger();
    spdlog::info("application starting");

    MainWindow window;
    window.setWindowTitle("Peripheral Capturer");
    window.resize(1280, 720);
    window.show();
    Timer::init();
    const int code = app.exec();
    spdlog::info("application exiting, code={}", code);
    shutdownLogger();
    return code;
}
