#include "MainWindow.h"

#include "../Input/GamepadProbe.h"

#include <QCheckBox>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QStatusBar>

#include <spdlog/spdlog.h>

MainWindow::MainWindow(QWidget* parent): QMainWindow(parent) {
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
