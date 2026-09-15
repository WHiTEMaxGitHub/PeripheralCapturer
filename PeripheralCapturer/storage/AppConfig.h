#pragma once

#include <QString>
#include <cstdint>

// 和 exe 同目录的 app-config.json。不要 QSettings。
struct AppConfig {
    bool recordKeyboard = true;
    bool recordMouse = true;
    bool recordGamepad = false;
    int recordingFps = 60;

    uint16_t recordingDeviceBits() const;
    static QString filePath();
    static AppConfig load();
    bool save() const;
};
