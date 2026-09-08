#include "Layout/MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    MainWindow window;
    window.setWindowTitle("Peripheral Capturer");
    window.resize(1280, 720);
    window.show();

    return app.exec();
}
