#pragma once

#include <cstdint>

// 和 exe 同目录的 app-config.json。不要 QSettings。
struct AppConfig {
    bool recordKeyboard = true;
    bool recordMouse = true;
    bool recordGamepad = false;

    uint16_t recordingDeviceBits() const;
    static AppConfig load();
    bool save() const;
};
