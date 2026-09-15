#pragma once

#include "ui_MainWindow.h"
#include "../Input/DeviceRegistry.h"
#include "../storage/AppConfig.h"

#include <QMainWindow>
#include <QTimer>
#include <cstdint>

class Database;
class InputPipeline;
class PovWindow;

class MainWindow: public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Database& database, InputPipeline& pipeline, QWidget* parent = nullptr);
    ~MainWindow() override;

    uint16_t recordingDeviceBits() const;
    void setPovWindow(PovWindow* pov);

private:
    enum Page : int {
        PageDevices = 0,
        PageKeyCodes,
        PageProfile,
        PageRecordings,
        PageLog,
        PageDebug,
    };

    void setupChrome();
    void setupRecordTargets();
    void applyRecordTargetsToUi();
    void onRecordTargetChanged();
    void refreshGamepadList();
    void pollGamepads();
#ifndef NDEBUG
    void setupDebugPage();
    void refreshDebugStats();
    void onDebugRebuildDatabase();
    void onDebugResetAppConfig();
    void onDebugOpenAppDir();
    void onDebugOpenLogDir();
    void onDebugTogglePovClick();
#endif

    Ui::MainWindowClass ui;
    Database& database_;
    InputPipeline& pipeline_;
    PovWindow* pov_ = nullptr;
    DeviceRegistry registry_;
    AppConfig appConfig_;
    bool xinputConnected_[4] = {};
    QTimer padTimer_;
#ifndef NDEBUG
    QTimer debugTimer_;
#endif
};
