#include "MainWindow.h"

#include "../Input/GamepadProbe.h"
#include "../Input/InputPipeline.h"
#include "../overlay/PovWindow.h"
#include "../storage/Database.h"
#include "../storage/Recorder.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QUrl>

#include <spdlog/spdlog.h>

MainWindow::MainWindow(Database& database, InputPipeline& pipeline, Recorder& recorder, QWidget* parent)
    : QMainWindow(parent)
    , database_(database)
    , pipeline_(pipeline)
    , recorder_(recorder) {
    ui.setupUi(this);
    setupChrome();
    setupRecordTargets();

    connect(ui.LeftSideBar, &QListWidget::currentRowChanged,
            ui.stackedWidget, &QStackedWidget::setCurrentIndex);
    ui.LeftSideBar->setCurrentRow(PageDevices);

    connect(&padTimer_, &QTimer::timeout, this, &MainWindow::pollGamepads);
    padTimer_.start(1000);
    pollGamepads();

    spdlog::info("[ui] MainWindow init bits={:#x}", recordingDeviceBits());
}

MainWindow::~MainWindow() {
    spdlog::info("[ui] MainWindow destroyed");
}

void MainWindow::setupChrome() {
    setWindowTitle(QStringLiteral("Peripheral Capturer"));

    ui.LeftSideBar->setFixedWidth(200);
    ui.LeftSideBar->setSpacing(2);
    ui.LeftSideBar->setStyleSheet(QStringLiteral(
        "QListWidget { background: #2b2b2b; color: #e8e8e8; padding: 8px 0; }"
        "QListWidget::item { min-height: 36px; padding-left: 16px; }"
        "QListWidget::item:selected { background: #3d6b99; }"
        "QListWidget::item:hover { background: #3a3a3a; }"));

    const auto applyTitle = [](QLabel* title) {
        QFont font = title->font();
        font.setPointSize(16);
        font.setBold(true);
        title->setFont(font);
    };
    applyTitle(ui.titleDevices);
    applyTitle(ui.titleKeyCodes);
    applyTitle(ui.titleProfile);
    applyTitle(ui.titleRecordings);
    applyTitle(ui.titleLog);

    ui.hintDevices->setStyleSheet(QStringLiteral("color: #888;"));
    ui.hintKeyCodes->setStyleSheet(QStringLiteral("color: #888;"));
    ui.hintProfile->setStyleSheet(QStringLiteral("color: #888;"));
    ui.hintRecordings->setStyleSheet(QStringLiteral("color: #888;"));
    ui.hintLog->setStyleSheet(QStringLiteral("color: #888;"));

#ifndef NDEBUG
    applyTitle(ui.titleDebug);
    ui.hintDebug->setStyleSheet(QStringLiteral("color: #888;"));
    setupDebugPage();
#else
    // Release 不要出现调试入口；stacked 里仍留着 pageDebug，只是点不到。
    for (int i = 0; i < ui.LeftSideBar->count(); ++i) {
        if (ui.LeftSideBar->item(i)->text() == QStringLiteral("调试")) {
            delete ui.LeftSideBar->takeItem(i);
            break;
        }
    }
    ui.pageDebug->hide();
#endif

    statusBar()->showMessage(QStringLiteral("就绪"));
}

void MainWindow::setupRecordTargets() {
    if (!ui.recordKeyboard || !ui.recordMouse || !ui.recordGamepad) {
        spdlog::error("[ui] record target checkboxes missing, rebuild MainWindow.ui");
        return;
    }
    appConfig_ = AppConfig::load();
    applyRecordTargetsToUi();
    connect(ui.recordKeyboard, &QCheckBox::toggled, this, &MainWindow::onRecordTargetChanged);
    connect(ui.recordMouse, &QCheckBox::toggled, this, &MainWindow::onRecordTargetChanged);
    connect(ui.recordGamepad, &QCheckBox::toggled, this, &MainWindow::onRecordTargetChanged);
}

void MainWindow::applyRecordTargetsToUi() {
    const QSignalBlocker b1(ui.recordKeyboard);
    const QSignalBlocker b2(ui.recordMouse);
    const QSignalBlocker b3(ui.recordGamepad);
    ui.recordKeyboard->setChecked(appConfig_.recordKeyboard);
    ui.recordMouse->setChecked(appConfig_.recordMouse);
    ui.recordGamepad->setChecked(appConfig_.recordGamepad);
}

uint16_t MainWindow::recordingDeviceBits() const {
    return appConfig_.recordingDeviceBits();
}

void MainWindow::onRecordTargetChanged() {
    appConfig_.recordKeyboard = ui.recordKeyboard->isChecked();
    appConfig_.recordMouse = ui.recordMouse->isChecked();
    appConfig_.recordGamepad = ui.recordGamepad->isChecked();
    if (appConfig_.recordingDeviceBits() == 0) {
        spdlog::warn("[ui] record targets empty, keep last valid");
        applyRecordTargetsToUi();
        return;
    }
    appConfig_.save();
}

void MainWindow::pollGamepads() {
    enumerateHidGamepads(registry_);
    pollXInputSlots(registry_, xinputConnected_);
    refreshGamepadList();
}

void MainWindow::refreshGamepadList() {
    ui.deviceList->clear();
    int xinputCount = 0;
    for (DWORD slot = 0; slot < 4; ++slot) {
        const auto id = registry_.getXInputDeviceID(slot);
        if (xinputConnected_[slot]) {
            ++xinputCount;
            ui.deviceList->addItem(QString::fromStdString(id + "  已连接"));
        } else {
            ui.deviceList->addItem(QString::fromStdString(id + "  空"));
        }
    }
    int hidCount = 0;
    for (const auto& info : registry_.snapshot()) {
        if (info.type != InputDeviceType::Hid) {
            continue;
        }
        ++hidCount;
        QString line = QString::fromStdString(info.deviceID);
        if (info.likelyXInput) {
            line += QStringLiteral("  （IG_，走 XInput）");
        }
        ui.deviceList->addItem(line);
    }
    statusBar()->showMessage(
        QStringLiteral("XInput %1/4  ·  HID 手柄 %2").arg(xinputCount).arg(hidCount));
}

void MainWindow::setPovWindow(PovWindow* pov) {
    pov_ = pov;
#ifndef NDEBUG
    refreshDebugStats();
#endif
}

#ifndef NDEBUG
void MainWindow::setupDebugPage() {
    connect(ui.debugRebuildDb, &QPushButton::clicked, this, &MainWindow::onDebugRebuildDatabase);
    connect(ui.debugResetAppConfig, &QPushButton::clicked, this, &MainWindow::onDebugResetAppConfig);
    connect(ui.debugOpenAppDir, &QPushButton::clicked, this, &MainWindow::onDebugOpenAppDir);
    connect(ui.debugOpenLogDir, &QPushButton::clicked, this, &MainWindow::onDebugOpenLogDir);
    connect(ui.debugTogglePovClick, &QPushButton::clicked, this, &MainWindow::onDebugTogglePovClick);
    connect(ui.debugToggleRecord, &QPushButton::clicked, this, &MainWindow::onDebugToggleRecord);
    connect(&debugTimer_, &QTimer::timeout, this, [this] {
        if (ui.LeftSideBar->currentRow() == PageDebug) {
            refreshDebugStats();
        }
    });
    debugTimer_.start(1000);
    refreshDebugStats();
}

void MainWindow::refreshDebugStats() {
    const auto st = database_.stats();
    const QString path = st.path.isEmpty() ? QStringLiteral("（无路径）") : st.path;
    const QString povLine = pov_
        ? QStringLiteral("POV 穿透：%1").arg(pov_->clickThrough() ? QStringLiteral("开")
                                                                   : QStringLiteral("关"))
        : QStringLiteral("POV：尚未挂上");
    // 不要用 %10：链式 arg 会把 %10 当成 %1。
    ui.debugStats->setText(
        QStringLiteral("库：%1\n").arg(path) +
        QStringLiteral("open=%1  schema=%2  key_codes=%3  sessions=%4  frames=%5  markers=%6\n")
            .arg(st.open ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(st.schemaVersion)
            .arg(st.keyCodes)
            .arg(st.sessions)
            .arg(st.frames)
            .arg(st.markers) +
        QStringLiteral("采集：running=%1  published=%2  packetDropped=%3\n")
            .arg(pipeline_.running() ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(pipeline_.publishedCount())
            .arg(static_cast<qulonglong>(pipeline_.packetDropped())) +
        QStringLiteral("录制：%1  session=%2  fps=%3\n")
            .arg(recorder_.recording() ? QStringLiteral("on") : QStringLiteral("off"))
            .arg(recorder_.sessionId())
            .arg(appConfig_.recordingFps) +
        povLine);
}

void MainWindow::onDebugRebuildDatabase() {
    const auto ret = QMessageBox::warning(
        this,
        QStringLiteral("删除并重建数据库"),
        QStringLiteral("将关掉连接并删除 data.db（含 WAL）。会话、帧和自定义码本都会没，builtin 码本会重新种上。\n\n确定？"),
        QMessageBox::Ok | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (ret != QMessageBox::Ok) {
        return;
    }
    recorder_.prepareForDbWipe();
    if (!database_.recreate()) {
        spdlog::error("[ui] debug recreate database failed");
        QMessageBox::critical(this, QStringLiteral("重建失败"),
                              QStringLiteral("删库或重新 open 失败，看 log/。若 DB Browser 开着这个文件，先关掉。"));
        refreshDebugStats();
        return;
    }
    spdlog::info("[ui] debug database recreated");
    statusBar()->showMessage(QStringLiteral("数据库已重建"), 4000);
    refreshDebugStats();
}

void MainWindow::onDebugResetAppConfig() {
    const QString path = AppConfig::filePath();
    if (QFile::exists(path) && !QFile::remove(path)) {
        spdlog::warn("[ui] debug cannot delete app-config path={}", path.toStdString());
        QMessageBox::warning(this, QStringLiteral("恢复失败"),
                             QStringLiteral("删不掉 app-config.json。"));
        return;
    }
    appConfig_ = AppConfig::load();
    applyRecordTargetsToUi();
    spdlog::info("[ui] debug app-config reset bits={:#x}", appConfig_.recordingDeviceBits());
    statusBar()->showMessage(QStringLiteral("已恢复默认 app-config"), 4000);
    refreshDebugStats();
}

void MainWindow::onDebugOpenAppDir() {
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QCoreApplication::applicationDirPath()));
}

void MainWindow::onDebugOpenLogDir() {
    const QString logDir = QCoreApplication::applicationDirPath() + QStringLiteral("/log");
    QDir().mkpath(logDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(logDir));
}

void MainWindow::onDebugTogglePovClick() {
    if (!pov_) {
        spdlog::warn("[ui] debug toggle POV: window not set");
        return;
    }
    pov_->setClickThrough(!pov_->clickThrough());
    refreshDebugStats();
}

void MainWindow::onDebugToggleRecord() {
    recorder_.toggle();
    statusBar()->showMessage(QStringLiteral("已发送开/停录（F9）"), 2000);
    refreshDebugStats();
}
#endif
