#include "AppConfig.h"

#include "RecordingLayout.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <spdlog/spdlog.h>

QString AppConfig::filePath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("app-config.json"));
}

uint16_t AppConfig::recordingDeviceBits() const {
    uint16_t bits = 0;
    if (recordKeyboard) {
        bits |= RecLayout::kBitKeyboard;
    }
    if (recordMouse) {
        bits |= RecLayout::kBitMouse;
    }
    if (recordGamepad) {
        bits |= RecLayout::kBitXInput;
    }
    return bits;
}

AppConfig AppConfig::load() {
    AppConfig cfg;
    const QString path = filePath();
    QFile file(path);
    if (!file.exists()) {
        spdlog::info("[ui] app-config missing, using keyboard+mouse");
        return cfg;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        spdlog::warn("[ui] app-config open failed path={}", path.toStdString());
        return cfg;
    }
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        spdlog::warn("[ui] app-config invalid json, keep defaults");
        return cfg;
    }
    const QJsonObject recording = doc.object().value(QStringLiteral("recording")).toObject();
    if (recording.contains(QStringLiteral("keyboard"))) {
        cfg.recordKeyboard = recording.value(QStringLiteral("keyboard")).toBool(true);
    }
    if (recording.contains(QStringLiteral("mouse"))) {
        cfg.recordMouse = recording.value(QStringLiteral("mouse")).toBool(true);
    }
    if (recording.contains(QStringLiteral("gamepad"))) {
        cfg.recordGamepad = recording.value(QStringLiteral("gamepad")).toBool(false);
    }
    if (cfg.recordingDeviceBits() == 0) {
        spdlog::warn("[ui] app-config record targets empty, fallback keyboard+mouse");
        cfg.recordKeyboard = true;
        cfg.recordMouse = true;
    }
    spdlog::info("[ui] app-config loaded bits={:#x}", cfg.recordingDeviceBits());
    return cfg;
}

bool AppConfig::save() const {
    const QString path = filePath();
    QJsonObject root;
    {
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly)) {
            const auto doc = QJsonDocument::fromJson(existing.readAll());
            if (doc.isObject()) {
                root = doc.object();
            }
        }
    }
    QJsonObject recording = root.value(QStringLiteral("recording")).toObject();
    recording.insert(QStringLiteral("keyboard"), recordKeyboard);
    recording.insert(QStringLiteral("mouse"), recordMouse);
    recording.insert(QStringLiteral("gamepad"), recordGamepad);
    root.insert(QStringLiteral("recording"), recording);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        spdlog::error("[ui] app-config save failed path={}", path.toStdString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    spdlog::info("[ui] app-config saved bits={:#x}", recordingDeviceBits());
    return true;
}
