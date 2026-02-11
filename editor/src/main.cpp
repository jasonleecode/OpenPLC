// 程序入口

#include "app/MainWindow.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // 设置应用程序信息
    app.setApplicationName("TiZi PLC Editor");
    app.setApplicationDisplayName("TiZi - 梯子");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("TiZiTeam");

    // 启动主窗口
    MainWindow window;
    window.resize(1280, 800); // 默认高清尺寸
    window.show();

    return app.exec();
}
