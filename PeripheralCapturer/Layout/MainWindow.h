#pragma once

#include "ui_MainWindow.h"
#include "../Input/DeviceRegistry.h"
#include "../storage/AppConfig.h"

#include <QMainWindow>
#include <QTimer>
#include <cstdint>

class MainWindow: public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    uint16_t recordingDeviceBits() const;

private:
    enum Page : int {
        PageDevices = 0,
        PageKeyCodes,
        PageProfile,
        PageRecordings,
        PageLog,
    };

    void setupChrome();
    void setupRecordTargets();
    void applyRecordTargetsToUi();
    void onRecordTargetChanged();
    void refreshGamepadList();
    void pollGamepads();

    Ui::MainWindowClass ui;
    DeviceRegistry registry_;
    AppConfig appConfig_;
    bool xinputConnected_[4] = {};
    QTimer padTimer_;
};
