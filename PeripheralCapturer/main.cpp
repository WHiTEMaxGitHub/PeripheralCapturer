#include "Layout/MainWindow.h"
#include "overlay/PovWindow.h"
#include "overlay/ViteDevServer.h"
#include "capture/HiddenCaptureWindow.h"
#include "utils/Logger.h"
#include "Input/InputPipeline.h"
#include "Input/Timer.h"
#include "storage/Database.h"
#include "storage/Recorder.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
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

#ifndef NDEBUG
    ViteDevServer vite;
    vite.start();
#endif

    Database database;
    const QString dbPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data.db"));
    if (!database.open(dbPath)) {
        spdlog::critical("[app] database init failed, abort");
#ifndef NDEBUG
        vite.stop();
#endif
        shutdownLogger();
        return 1;
    }

    // 三扇窗：捕获（不可见）→ 配置 → POV，互不嵌套。
    InputPipeline pipeline;
    // 总线不能退订；Recorder 必须在 start 前订上，否则 Block 队列没人 pop。
    Recorder recorder(pipeline.bus(), dbPath);
    HiddenCaptureWindow capture;
    if (!capture.create(pipeline.packets())) {
        spdlog::critical("[app] capture window init failed, abort");
        recorder.stop();
        database.close();
#ifndef NDEBUG
        vite.stop();
#endif
        shutdownLogger();
        return 1;
    }
    pipeline.setOnToggleRecord([&recorder] { recorder.toggle(); });
    pipeline.start();

    MainWindow config(database, pipeline, recorder);
    config.resize(1280, 720);
    config.show();
    spdlog::info("[app] config window shown");

    PovWindow pov;
#ifndef NDEBUG
    QObject::connect(&vite, &ViteDevServer::ready, &pov, [&pov](bool ok) {
        if (ok) {
            pov.loadDevPage();
        } else {
            pov.loadOfflineHtml();
        }
    });
#endif
    pov.setClickThrough(true);
    pov.show();
    config.setPovWindow(&pov);
    // 处理线程不能碰 Qt 窗体：F10 排队到 GUI。
    pipeline.setOnTogglePovClick([&pov] {
        QMetaObject::invokeMethod(
            &pov,
            [&pov] { pov.setClickThrough(!pov.clickThrough()); },
            Qt::QueuedConnection);
    });
    spdlog::info("[app] pov window shown");

    const int code = app.exec();

    pipeline.setOnToggleRecord({});
    pipeline.setOnTogglePovClick({});
    capture.destroy();
    pipeline.stop();
    recorder.stop();
    database.close();
    if (code != 0) {
        spdlog::error("[app] exiting code={}", code);
    } else {
        spdlog::info("[app] exiting code=0");
    }
#ifndef NDEBUG
    vite.stop();
#endif
    shutdownLogger();
    return code;
}
