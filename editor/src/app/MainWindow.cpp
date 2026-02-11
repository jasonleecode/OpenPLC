#include "MainWindow.h"

// Qt UI 组件
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QDockWidget>
#include <QTextEdit>
#include <QLabel>
#include <QToolBar>
#include <QActionGroup>
#include <QMessageBox>
#include <QTextEdit>
#include <QDialog>
#include <QVBoxLayout>

// 自定义编辑器组件
#include "../editor/scene/LadderScene.h"
#include "../editor/scene/LadderView.h"
#include "../editor/items/ContactItem.h"
#include "../core/compiler/CodeGenerator.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setupUi();
}

MainWindow::~MainWindow() {
}

void MainWindow::setupUi() {
    // 1. 创建场景 (Scene) - 这是我们的"纸"
    LadderScene* scene = new LadderScene(this);

    // 2. 创建视图 (View) - 这是我们的"放大镜"
    LadderView* view = new LadderView(this);
    view->setScene(scene);

    // 3. 将视图设为中心部件
    // 这样视图会自动填满整个窗口，并随着窗口调整大小
    setCentralWidget(view);

    menuBar()->setNativeMenuBar(false);

    // 5. 菜单栏
    QMenu* fileMenu = menuBar()->addMenu("文件(&F)");
    fileMenu->addAction("新建工程", [](){ /* 以后实现 */ });
    fileMenu->addAction("打开工程...", [](){ /* 以后实现 */ });
    fileMenu->addSeparator();
    fileMenu->addAction("退出", this, &QWidget::close);

    QMenu* buildMenu = menuBar()->addMenu("编译(&B)");
    buildMenu->addAction("生成代码 (B)", [](){ /* 以后实现 */ });
    buildMenu->addAction("下载到 PLC", [](){ /* 以后实现 */ });
    buildMenu->addAction("生成代码 (Generate Code)", [this, scene](){
        // 1. 调用编译器
        QString code = CodeGenerator::generate(scene);

        // 2. 创建一个对话框显示代码
        QDialog* dialog = new QDialog(this);
        dialog->setWindowTitle("生成的 C 代码");
        dialog->resize(600, 500);

        QVBoxLayout* layout = new QVBoxLayout(dialog);
        QTextEdit* textEdit = new QTextEdit(dialog);
        textEdit->setPlainText(code);
        textEdit->setReadOnly(true);
        textEdit->setFont(QFont("Courier New", 12)); // 等宽字体看起来像代码

        layout->addWidget(textEdit);
        dialog->exec();
    });

    // ==========================================
    // 4. 【新增】工具栏 (Toolbar)
    // ==========================================
    QToolBar* toolbar = addToolBar("Tools");
    toolbar->setMovable(false); // 固定工具栏

    // 创建一个互斥组，确保同一时间只能选一个工具
    QActionGroup* modeGroup = new QActionGroup(this);

    // --- 选择工具 ---
    QAction* actSelect = toolbar->addAction("选择 (Select)");
    actSelect->setCheckable(true);
    actSelect->setChecked(true); // 默认选中
    modeGroup->addAction(actSelect);
    // 连接信号：切回选择模式
    connect(actSelect, &QAction::triggered, [scene](){
        scene->setMode(Mode_Select);
    });

    // --- 添加常开触点 ---
    QAction* actAddNO = toolbar->addAction("常开 -| |-");
    actAddNO->setCheckable(true);
    modeGroup->addAction(actAddNO);
    connect(actAddNO, &QAction::triggered, [scene](){
        scene->setMode(Mode_AddContact_NO);
    });

    // --- 添加常闭触点 ---
    QAction* actAddNC = toolbar->addAction("常闭 -|/|-");
    actAddNC->setCheckable(true);
    modeGroup->addAction(actAddNC);
    connect(actAddNC, &QAction::triggered, [scene](){
        scene->setMode(Mode_AddContact_NC);
    });

    // --- 添加线圈 ---
    QAction* actAddCoil = toolbar->addAction("线圈 -( )");
    actAddCoil->setCheckable(true);
    modeGroup->addAction(actAddCoil);
    connect(actAddCoil, &QAction::triggered, [scene](){
        scene->setMode(Mode_AddCoil);
    });

    // --- 添加导线 ---
    QAction* actAddWire = toolbar->addAction("导线 (Wire)");
    actAddWire->setCheckable(true);
    modeGroup->addAction(actAddWire);
    connect(actAddWire, &QAction::triggered, [scene](){
        scene->setMode(Mode_AddWire);
    });

    // 4. 状态栏
    statusBar()->showMessage("TiZi 画布就绪 - 按住 Ctrl+滚轮缩放，中键平移");
}