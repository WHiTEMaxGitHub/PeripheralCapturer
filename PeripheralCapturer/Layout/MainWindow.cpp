#include "MainWindow.h"
#include <spdlog/spdlog.h>
MainWindow::MainWindow(QWidget* parent): QMainWindow(parent) {
    ui.setupUi(this);
    ui.LeftSideBar->addItem("Test Page");
    ui.LeftSideBar->addItem("Main Page");
    ui.LeftSideBar->addItem("Profile");
    ui.LeftSideBar->addItem("Recording File");
    ui.LeftSideBar->addItem("Log");

    connect(ui.LeftSideBar, &QListWidget::currentRowChanged,
            ui.stackedWidget, &QStackedWidget::setCurrentIndex);

    ui.LeftSideBar->setCurrentRow(0);
    spdlog::info("MainWindow created");
}

MainWindow::~MainWindow() {}
