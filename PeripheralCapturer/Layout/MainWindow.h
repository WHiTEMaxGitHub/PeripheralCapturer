#pragma once

#include "ui_MainWindow.h"
#include "../Input/DeviceRegistry.h"

#include <QMainWindow>
#include <QTimer>

class MainWindow: public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    enum Page : int {
        PageDevices = 0,
        PageKeyCodes,
        PageProfile,
        PageRecordings,
        PageLog,
    };

    void setupChrome();
    void refreshGamepadList();
    void pollGamepads();

    Ui::MainWindowClass ui;
    DeviceRegistry registry_;
    bool xinputConnected_[4] = {};
    QTimer padTimer_;
};
