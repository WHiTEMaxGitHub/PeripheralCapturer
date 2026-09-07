#include <QApplication>
#include <QWidget>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("Peripheral Capturer");
    window.resize(1280, 720);
    window.show();

    return app.exec();
}