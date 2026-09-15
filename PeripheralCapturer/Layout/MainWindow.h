#pragma once

#include "ui_MainWindow.h"
#include "../Input/BoundedQueue.h"
#include "../Input/DeviceRegistry.h"
#include "../Input/InputEvent.h"
#include "../storage/AppConfig.h"
#include "../storage/Database.h"

#include <QMainWindow>
#include <QTimer>
#include <cstdint>
#include <memory>
#include <optional>

class InputPipeline;
class PovWindow;
class Recorder;

class MainWindow: public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Database& database, InputPipeline& pipeline, Recorder& recorder,
                        QWidget* parent = nullptr);
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

    void setupKeyCodesPage();
    void refreshKeyCodesTable();
    void reloadPipelineCodebook();
    void updateKeyCodeActions();
    std::optional<qint64> selectedKeyCodeId() const;
    std::optional<KeyCodeRecord> selectedKeyCode() const;
    void onKeyCodeAdd();
    void onKeyCodeEdit();
    void onKeyCodeDelete();
    void onKeyCodeBind();
    void startBindListen(qint64 id);
    void stopBindListen(const QString& status);
    void pollBindListen();
    bool applyBindEvent(const InputEvent& event);

#ifndef NDEBUG
    void setupDebugPage();
    void refreshDebugStats();
    void onDebugRebuildDatabase();
    void onDebugResetAppConfig();
    void onDebugOpenAppDir();
    void onDebugOpenLogDir();
    void onDebugTogglePovClick();
    void onDebugToggleRecord();
#endif

    Ui::MainWindowClass ui;
    Database& database_;
    InputPipeline& pipeline_;
    Recorder& recorder_;
    PovWindow* pov_ = nullptr;
    DeviceRegistry registry_;
    AppConfig appConfig_;
    bool xinputConnected_[4] = {};
    QTimer padTimer_;

    std::shared_ptr<BoundedQueue<InputEvent>> bindQueue_;
    QTimer bindListenTimer_;
    qint64 bindListenTargetId_ = 0;
    qint64 bindListenUntilMs_ = 0;
    bool bindListening_ = false;
#ifndef NDEBUG
    QTimer debugTimer_;
#endif
};
