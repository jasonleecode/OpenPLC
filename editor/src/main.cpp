// 程序入口

#include "app/MainWindow.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("TiZi PLC Editor");
    app.setApplicationDisplayName("TiZi - 梯子");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("TiZiTeam");

    MainWindow window;
    window.show();

    return app.exec();
}
