#include "Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>

#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

void initLogger() {
    const QString logDir = QCoreApplication::applicationDirPath() + QStringLiteral("/log");
    if (!QDir().mkpath(logDir)) {
        // 此时 default logger 可能还不存在，先建控制台再报错。
    }

    const QString fileName = QStringLiteral("peripheral-capturer-%1.log")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd")));
    const std::string filePath = QDir(logDir).filePath(fileName).toStdString();

    spdlog::init_thread_pool(8192, 1);

    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> file;
    try {
        file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filePath, 5 * 1024 * 1024, 3);
    } catch (const spdlog::spdlog_ex& ex) {
        auto fallback = std::make_shared<spdlog::async_logger>(
            "pc", console, spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);
        spdlog::set_default_logger(fallback);
        spdlog::critical("[log] rotating file sink failed: {}", ex.what());
        return;
    }

    std::vector<spdlog::sink_ptr> sinks{console, file};
    auto logger = std::make_shared<spdlog::async_logger>(
        "pc",
        sinks.begin(),
        sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::overrun_oldest);

    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
#ifdef NDEBUG
    logger->set_level(spdlog::level::info);
    console->set_level(spdlog::level::info);
    file->set_level(spdlog::level::info);
#else
    logger->set_level(spdlog::level::debug);
    console->set_level(spdlog::level::debug);
    file->set_level(spdlog::level::debug);
#endif
    logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(logger);
    spdlog::info("[log] init ok file={}", filePath);
}

void shutdownLogger() {
    spdlog::info("[log] shutdown");
    spdlog::shutdown();
}
