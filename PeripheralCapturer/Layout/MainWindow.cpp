#include "MainWindow.h"

#include "../Input/GamepadProbe.h"

#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QStatusBar>

#include <spdlog/spdlog.h>

MainWindow::MainWindow(QWidget* parent): QMainWindow(parent) {
    ui.setupUi(this);
    setupChrome();

    connect(ui.LeftSideBar, &QListWidget::currentRowChanged,
            ui.stackedWidget, &QStackedWidget::setCurrentIndex);
    ui.LeftSideBar->setCurrentRow(PageDevices);

    connect(&padTimer_, &QTimer::timeout, this, &MainWindow::pollGamepads);
    padTimer_.start(1000);
    pollGamepads();

    spdlog::info("[ui] MainWindow init");
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
