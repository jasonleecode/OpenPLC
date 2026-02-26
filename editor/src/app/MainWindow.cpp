#include "MainWindow.h"

#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QStatusBar>
#include <QDockWidget>
#include <QToolBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFont>
#include <QFile>
#include <QMenu>
#include <QTimer>
#include <QDomDocument>
#include <QCoreApplication>
#include <QUndoStack>
#include <QFrame>
#include <QEvent>
#include <QGroupBox>
#include <QApplication>

#include "../editor/scene/LadderScene.h"
#include "../editor/scene/LadderView.h"
#include "../editor/scene/PlcOpenViewer.h"
#include "../utils/StHighlighter.h"
#include "../utils/TreeBranchStyle.h"
#include "../core/compiler/CodeGenerator.h"
#include "../core/compiler/StGenerator.h"
#include "../comm/DownloadDialog.h"

// PlcOpenViewer 兼作所有图形语言（LD/FBD/SFC）的统一编辑器

// ============================================================
// ============================================================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    menuBar()->setNativeMenuBar(false);

    // 初始化 UI 框架
    setupMenuBar();
    setupToolBar();
    setupProjectPanel();
    setupLibraryPanel();
    setupConsolePanel();
    setupCentralArea();
    setupStatusBar();

    // 加载样式表
    applyTheme(":/light_theme.qss");   // 默认浅色主题

    // 创建默认项目
    m_project = new ProjectModel(this);
    connect(m_project, &ProjectModel::changed, this, &MainWindow::updateWindowTitle);
    buildDefaultProject();

    resize(1400, 900);
    updateWindowTitle();
}

MainWindow::~MainWindow()
{
    // ── 析构顺序问题修复 ────────────────────────────────────────
    // C++ 析构顺序：① 析构体执行 → ② 成员变量析构 → ③ 基类析构
    // Qt 在 ③（~QObject）中才删除子对象（deleteChildren）。
    //
    // 问题：m_subWinPouMap（值类型 QMap，成员变量）在 ② 被销毁，
    // 但 ③ 删除 MDI 子窗口时触发 destroyed 信号，lambda 仍试图
    // 访问 m_subWinPouMap.remove(sw) → use-after-destruction → SIGSEGV。
    //
    // 修复：在 ① 中提前断开所有子窗口的 destroyed 信号，让 ② 之后
    // 的子窗口删除不再触发 lambda。
    if (m_mdiArea) {
        for (QMdiSubWindow* sw : m_mdiArea->subWindowList())
            sw->disconnect(this);
    }
}

// ============================================================
// 默认示例项目
// ============================================================
void MainWindow::buildDefaultProject() {
    m_project->projectName = "First Steps";

    // CounterLD
    PouModel* ld = m_project->addPou("CounterLD", PouType::FunctionBlock, PouLanguage::LD);
    ld->description = "Counter using Ladder Diagram";
    ld->variables.append({"Reset", "Input",  "BOOL", "", ""});
    ld->variables.append({"Out",   "Output", "INT",  "", ""});

    // CounterST
    PouModel* st = m_project->addPou("CounterST", PouType::FunctionBlock, PouLanguage::ST);
    st->description = "Counter using Structured Text";
    st->variables.append({"Reset",             "Input",  "BOOL", "", ""});
    st->variables.append({"Out",               "Output", "INT",  "", ""});
    st->variables.append({"Cnt",               "Local",  "INT",  "", ""});
    st->variables.append({"ResetCounterValue", "Input",  "INT",  "", ""});
    st->code =
        "IF Reset THEN\n"
        "    Cnt := ResetCounterValue;\n"
        "ELSE\n"
        "    Cnt := Cnt + 1;\n"
        "END_IF;\n"
        "\n"
        "Out := Cnt;";

    // CounterIL
    PouModel* il = m_project->addPou("CounterIL", PouType::FunctionBlock, PouLanguage::IL);
    il->description = "Counter using Instruction List";
    il->variables.append({"Reset",             "Input",  "BOOL", "", ""});
    il->variables.append({"Out",               "Output", "INT",  "", ""});
    il->variables.append({"Cnt",               "Local",  "INT",  "", ""});
    il->variables.append({"ResetCounterValue", "Input",  "INT",  "", ""});
    il->code =
        "LD  Reset\n"
        "JMPC ResetCnt\n"
        "(* increment counter *)\n"
        "LD  Cnt\n"
        "ADD 1\n"
        "JMP QuitFb\n"
        "\n"
        "ResetCnt:\n"
        "(* reset counter *)\n"
        "LD  ResetCounterValue\n"
        "\n"
        "QuitFb:\n"
        "(* save results *)\n"
        "ST  Cnt\n"
        "ST  Out";

    m_project->clearDirty();

    // 构建树并打开第一个 POU
    rebuildProjectTree();
    if (!m_project->pous.isEmpty())
        openPouTab(m_project->pous.first());
}

// ============================================================
// 菜单栏
// ============================================================
void MainWindow::setupMenuBar()
{
    QMenu* fileMenu = menuBar()->addMenu("File(&F)");
    fileMenu->addAction("New Project",    this, &MainWindow::newProject);
    fileMenu->addAction("Open Project...", this, &MainWindow::openProject);
    fileMenu->addAction("Save",           this, &MainWindow::saveProject);
    fileMenu->addAction("Save As...",     this, &MainWindow::saveProjectAs);
    fileMenu->addSeparator();
    fileMenu->addAction("Exit", this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu("Edit(&E)");
    editMenu->addAction(makeLdIcon("undo"),  "Undo",  this, []{
        if (auto* w = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget())) w->undo();
    }, QKeySequence::Undo);
    editMenu->addAction(makeLdIcon("redo"),  "Redo",  this, []{
        if (auto* w = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget())) w->redo();
    }, QKeySequence::Redo);
    editMenu->addSeparator();
    editMenu->addAction(makeLdIcon("cut"),   "Cut",   this, []{
        if (auto* w = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget())) w->cut();
    }, QKeySequence::Cut);
    editMenu->addAction(makeLdIcon("copy"),  "Copy",  this, []{
        if (auto* w = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget())) w->copy();
    }, QKeySequence::Copy);
    editMenu->addAction(makeLdIcon("paste"), "Paste", this, []{
        if (auto* w = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget())) w->paste();
    }, QKeySequence::Paste);

    // ── PLC 菜单 ─────────────────────────────────────────────
    QMenu* plcMenu = menuBar()->addMenu("PLC(&P)");

    // 编译组
    auto* aBuildActive = plcMenu->addAction(
        QIcon(":/images/Build.png"), "Build Active Resource\tCtrl+B");
    aBuildActive->setShortcut(QKeySequence("Ctrl+B"));
    connect(aBuildActive, &QAction::triggered, this, &MainWindow::buildProject);

    auto* aRebuild = plcMenu->addAction(
        QIcon(":/images/Clean.png"), "Rebuild Active Resource");
    connect(aRebuild, &QAction::triggered, this, [this]{
        m_consoleEdit->clear();
        buildProject();
    });

    plcMenu->addSeparator();

    // 连接组
    auto* aConn = plcMenu->addAction(
        QIcon(":/images/Connect.png"), "Connections...");
    connect(aConn, &QAction::triggered, this, &MainWindow::connectToPlc);

    auto* aOnline = plcMenu->addAction(
        QIcon(":/images/Connect.png"), "Online");
    aOnline->setCheckable(true);
    connect(aOnline, &QAction::triggered, this, &MainWindow::connectToPlc);

    plcMenu->addSeparator();

    // 传输/控制组
    auto* aDownload = plcMenu->addAction(
        QIcon(":/images/Transfer.png"), "Download...");
    connect(aDownload, &QAction::triggered, this, &MainWindow::downloadProject);

    auto* aColdStart = plcMenu->addAction("Cold Start");
    connect(aColdStart, &QAction::triggered, this, [this]{
        if (m_connState != PlcConnState::Connected) {
            QMessageBox::warning(this, "Not Connected", "Please connect to a PLC first.");
            return;
        }
        setPlcRunState(PlcRunState::Stopped);
        statusBar()->showMessage("Cold start requested.", 3000);
    });

    auto* aHotStart = plcMenu->addAction("Hot Start");
    connect(aHotStart, &QAction::triggered, this, [this]{
        if (m_connState != PlcConnState::Connected) {
            QMessageBox::warning(this, "Not Connected", "Please connect to a PLC first.");
            return;
        }
        setPlcRunState(PlcRunState::Running);
        statusBar()->showMessage("Hot start requested.", 3000);
    });

    auto* aPlcStop = plcMenu->addAction(
        QIcon(":/images/Stop.png"), "Stop");
    connect(aPlcStop, &QAction::triggered, this, [this]{
        if (m_connState != PlcConnState::Connected) {
            QMessageBox::warning(this, "Not Connected", "Please connect to a PLC first.");
            return;
        }
        setPlcRunState(PlcRunState::Stopped);
        statusBar()->showMessage("PLC stopped.", 3000);
    });

    plcMenu->addSeparator();

    // 调试组
    auto* aMonitor = plcMenu->addAction(
        QIcon(":/images/Debug.png"), "Monitor / Edit");
    aMonitor->setCheckable(true);
    connect(aMonitor, &QAction::triggered, this, [this](bool checked){
        if (m_connState != PlcConnState::Connected) {
            QMessageBox::warning(this, "Not Connected", "Please connect to a PLC first.");
            return;
        }
        statusBar()->showMessage(
            checked ? "Monitor mode enabled." : "Monitor mode disabled.", 3000);
    });

    auto* aBrowser = plcMenu->addAction(
        QIcon(":/images/IO_VARIABLE.png"), "Browser");
    connect(aBrowser, &QAction::triggered, this, [this]{
        QMessageBox::information(this, "Variable Browser",
            "Variable browser is not yet implemented.");
    });

    plcMenu->addSeparator();

    auto* aPlcInfo = plcMenu->addAction(
        QIcon(":/images/LOG_INFO.png"), "PLC Info...");
    connect(aPlcInfo, &QAction::triggered, this, [this]{
        if (m_connState != PlcConnState::Connected) {
            QMessageBox::information(this, "PLC Info",
                "Not connected to any PLC.\n"
                "Use Connections... to establish a connection first.");
            return;
        }
        QMessageBox::information(this, "PLC Info",
            QString("URI: %1\nStatus: Connected\nRuntime: OpenPLC Runtime")
                .arg(m_plcUri));
    });

    // ── Extras 菜单 ──────────────────────────────────────────
    QMenu* extrasMenu = menuBar()->addMenu("Extras(&X)");

    // Tools 子菜单
    QMenu* toolsMenu = extrasMenu->addMenu("Tools");
    auto* aDriverInstall = toolsMenu->addAction("Driver Install...");
    connect(aDriverInstall, &QAction::triggered, this, [this]{
        const QString path = QFileDialog::getOpenFileName(
            this, "Select Driver Package", QString(),
            "Driver Packages (*.cab *.zip);;All Files (*)");
        if (!path.isEmpty())
            QMessageBox::information(this, "Driver Install",
                QString("Driver installation is not yet implemented.\nSelected: %1").arg(path));
    });

    auto* aLicenseEditor = extrasMenu->addAction("License Editor");
    connect(aLicenseEditor, &QAction::triggered, this, [this]{
        QMessageBox::information(this, "License Editor",
            "License editor is not yet implemented.");
    });

    extrasMenu->addSeparator();

    auto* aOptions = extrasMenu->addAction(
        QIcon(":/images/CONFIGURATION.png"), "Options...");
    connect(aOptions, &QAction::triggered, this, [this]{
        QDialog dlg(this);
        dlg.setWindowTitle("Options");
        dlg.setMinimumWidth(400);

        QFormLayout form(&dlg);
        form.setContentsMargins(12, 12, 12, 8);
        form.setSpacing(8);

        // 编译器路径
        auto* compEdit = new QLineEdit(
            m_project ? m_project->compiler : "gcc");
        form.addRow("Compiler:", compEdit);

        auto* linkerEdit = new QLineEdit(
            m_project ? m_project->linker : "gcc");
        form.addRow("Linker:", linkerEdit);

        // 编辑器字体
        auto* fontSizeSpin = new QSpinBox();
        fontSizeSpin->setRange(7, 24);
        fontSizeSpin->setValue(
            m_consoleEdit ? m_consoleEdit->font().pointSize() : 10);
        form.addRow("Editor Font Size:", fontSizeSpin);

        auto* btns = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
            Qt::Horizontal, &dlg);
        form.addRow(btns);
        connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        if (dlg.exec() == QDialog::Accepted && m_project) {
            m_project->compiler = compEdit->text().trimmed();
            m_project->linker   = linkerEdit->text().trimmed();
            m_project->markDirty();
            // 应用字体到所有代码编辑器
            const int sz = fontSizeSpin->value();
            QFont f("Courier New", sz);
            if (m_consoleEdit) m_consoleEdit->setFont(f);
            statusBar()->showMessage("Options saved.", 3000);
        }
    });

    // ── Display 菜单 ─────────────────────────────────────────
    QMenu* displayMenu = menuBar()->addMenu("Display(&D)");
    displayMenu->addAction(makeLdIcon("zoom_in"),  "Zoom In",
                           this, &MainWindow::zoomIn,  QKeySequence(Qt::CTRL | Qt::Key_Equal));
    displayMenu->addAction(makeLdIcon("zoom_out"), "Zoom Out",
                           this, &MainWindow::zoomOut, QKeySequence(Qt::CTRL | Qt::Key_Minus));
    displayMenu->addAction(makeLdIcon("fit"),      "Fit to Window",
                           this, &MainWindow::zoomFit, QKeySequence(Qt::CTRL | Qt::Key_0));

    displayMenu->addSeparator();

    // ── 主题切换 ──────────────────────────────────────────────
    QMenu* themeMenu = displayMenu->addMenu("Theme");

    auto* aLightTheme = themeMenu->addAction("Light");
    aLightTheme->setCheckable(true);
    aLightTheme->setChecked(true);

    auto* aDarkTheme = themeMenu->addAction("Dark");
    aDarkTheme->setCheckable(true);
    aDarkTheme->setChecked(false);

    // 互斥：切到浅色
    connect(aLightTheme, &QAction::triggered, this, [this, aLightTheme, aDarkTheme]() {
        applyTheme(":/light_theme.qss");
        aLightTheme->setChecked(true);
        aDarkTheme->setChecked(false);
    });

    // 互斥：切到深色
    connect(aDarkTheme, &QAction::triggered, this, [this, aLightTheme, aDarkTheme]() {
        applyTheme(":/dark_theme.qss");
        aDarkTheme->setChecked(true);
        aLightTheme->setChecked(false);
    });

    QMenu* helpMenu = menuBar()->addMenu("Help(&H)");
    helpMenu->addAction("About", this, [this](){
        QMessageBox::about(this, "TiZi PLC Editor",
            "TiZi PLC Editor v0.1.0\n\nAn OpenPLC IDE inspired by Beremiz");
    });
}

// ============================================================
// 工具栏图标：优先加载 QRC 图片，无对应图片则用 QPainter 绘制
// ============================================================
QIcon MainWindow::makeLdIcon(const QString& type, int sz)
{
    // 直接从资源文件加载的图标（Beremiz 原版图标）
    static const QMap<QString, QString> s_pngMap = {
        // 文件操作
        { "new",        ":/images/new.png"         },
        { "open",       ":/images/open.png"        },
        { "save",       ":/images/save.png"        },
        { "saveas",     ":/images/saveas.png"      },
        // 编辑
        { "undo",       ":/images/undo.png"        },
        { "redo",       ":/images/redo.png"        },
        { "cut",        ":/images/cut.png"         },
        { "copy",       ":/images/copy.png"        },
        { "paste",      ":/images/paste.png"       },
        // 编译
        { "build",      ":/images/Build.png"       },
        { "clean",      ":/images/Clean.png"       },
        // PLC 连接/控制
        { "connect",    ":/images/Connect.png"     },
        { "disconnect", ":/images/Disconnect.png"  },
        { "download",   ":/images/Transfer.png"    },
        { "run",        ":/images/Run.png"         },
        { "stop",       ":/images/Stop.png"        },
        // LD 编辑元件
        { "select",     ":/images/select.png"      },
        { "no",         ":/images/add_contact.png" },
        { "coil",       ":/images/add_coil.png"    },
        { "fb",         ":/images/add_block.png"   },
        { "wire",       ":/images/add_wire.png"    },
        // 缩放
        { "zoom_in",    ":/images/zoom_in.png"     },
        { "zoom_out",   ":/images/zoom_out.png"    },
        { "fit",        ":/images/zoom_fit.png"    },
    };
    if (s_pngMap.contains(type))
        return QIcon(s_pngMap[type]);

    // QPainter 绘制：NC / P / N 触点，Set / Reset 线圈
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    const int cy = sz / 2;
    const int bl = sz / 4;
    const int br = sz * 3 / 4;
    const int bt = cy - sz / 4;
    const int bb = cy + sz / 4;

    QPen pen(QColor("#2A2A2A"), 1.5f);
    p.setPen(pen);

    if (type == "nc") {
        p.drawLine(2, cy, bl, cy);
        p.drawLine(br, cy, sz-2, cy);
        p.drawLine(bl, bt, bl, bb);
        p.drawLine(br, bt, br, bb);
        p.drawLine(bl+1, bb-1, br-1, bt+1);

    } else if (type == "pe" || type == "ne") {
        p.drawLine(2, cy, bl, cy);
        p.drawLine(br, cy, sz-2, cy);
        p.drawLine(bl, bt, bl, bb);
        p.drawLine(br, bt, br, bb);
        QFont f; f.setPixelSize(sz/3); f.setBold(true); p.setFont(f);
        p.drawText(QRectF(bl+1, bt, br-bl-2, bb-bt),
                   Qt::AlignCenter, type == "pe" ? "P" : "N");

    } else if (type == "set" || type == "rst") {
        const int al = bl - 2, ar = br + 2;
        p.drawLine(2, cy, al, cy);
        p.drawLine(ar, cy, sz-2, cy);
        p.drawArc(al, bt, 8, bb-bt+1,  90*16,  180*16);
        p.drawArc(ar-8, bt, 8, bb-bt+1, 90*16, -180*16);
        QFont f; f.setPixelSize(sz/3); f.setBold(true); p.setFont(f);
        p.drawText(QRectF(al+4, bt, ar-al-8, bb-bt),
                   Qt::AlignCenter, type == "set" ? "S" : "R");

    } else if (type == "zoom_in" || type == "zoom_out") {
        // 放大镜圆圈
        const int r = sz * 5 / 14;
        const int ox = sz * 4 / 10, oy = sz * 4 / 10;
        p.setPen(QPen(QColor("#2A2A2A"), 1.8));
        p.drawEllipse(QPoint(ox, oy), r, r);
        // 手柄
        int hx1 = ox + r * 7/10, hy1 = oy + r * 7/10;
        p.drawLine(hx1, hy1, sz-3, sz-3);
        // + 或 -
        int cross = r * 5 / 8;
        p.drawLine(ox - cross, oy, ox + cross, oy);
        if (type == "zoom_in")
            p.drawLine(ox, oy - cross, ox, oy + cross);

    } else if (type == "fit") {
        // 四角箭头：表示 Fit to Window
        p.setPen(QPen(QColor("#2A2A2A"), 1.5));
        int m = 3, a = 5;
        // 左上角
        p.drawLine(m, m+a, m, m); p.drawLine(m, m, m+a, m);
        // 右上角
        p.drawLine(sz-m, m+a, sz-m, m); p.drawLine(sz-m, m, sz-m-a, m);
        // 左下角
        p.drawLine(m, sz-m-a, m, sz-m); p.drawLine(m, sz-m, m+a, sz-m);
        // 右下角
        p.drawLine(sz-m, sz-m-a, sz-m, sz-m); p.drawLine(sz-m, sz-m, sz-m-a, sz-m);
    }

    return QIcon(pm);
}

// ============================================================
// 工具栏
// ============================================================
void MainWindow::setupToolBar()
{
    QToolBar* tb = addToolBar("Main");
    tb->setObjectName("mainToolBar");
    tb->setMovable(false);
    tb->setIconSize(QSize(24, 24));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // ══════════════════════════════════════════════════════════
    // 第1组：文件操作
    // ══════════════════════════════════════════════════════════
    auto* aNew    = tb->addAction(makeLdIcon("new"),    "New Project  [Ctrl+N]");
    auto* aOpen   = tb->addAction(makeLdIcon("open"),   "Open Project  [Ctrl+O]");
    auto* aSave   = tb->addAction(makeLdIcon("save"),   "Save  [Ctrl+S]");
    auto* aSaveAs = tb->addAction(makeLdIcon("saveas"), "Save As…");
    connect(aNew,    &QAction::triggered, this, &MainWindow::newProject);
    connect(aOpen,   &QAction::triggered, this, &MainWindow::openProject);
    connect(aSave,   &QAction::triggered, this, &MainWindow::saveProject);
    connect(aSaveAs, &QAction::triggered, this, &MainWindow::saveProjectAs);
    aNew->setShortcut(QKeySequence::New);
    aOpen->setShortcut(QKeySequence::Open);
    aSave->setShortcut(QKeySequence::Save);
    tb->addSeparator();

    // ══════════════════════════════════════════════════════════
    // 第2组：编辑（Undo / Redo）
    // ══════════════════════════════════════════════════════════
    m_aUndo = tb->addAction(makeLdIcon("undo"), "Undo  [Ctrl+Z]");
    m_aRedo = tb->addAction(makeLdIcon("redo"), "Redo  [Ctrl+Y]");
    m_aUndo->setShortcut(QKeySequence::Undo);
    m_aRedo->setShortcut(QKeySequence::Redo);
    m_aUndo->setEnabled(false);
    m_aRedo->setEnabled(false);
    // 优先委托图形场景 undo stack，无场景时转发给聚焦的文本控件
    connect(m_aUndo, &QAction::triggered, this, [this]{
        if (m_scene && m_scene->undoStack()->canUndo())
            m_scene->undoStack()->undo();
        else if (auto* w = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget()))
            w->undo();
    });
    connect(m_aRedo, &QAction::triggered, this, [this]{
        if (m_scene && m_scene->undoStack()->canRedo())
            m_scene->undoStack()->redo();
        else if (auto* w = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget()))
            w->redo();
    });
    tb->addSeparator();

    // ══════════════════════════════════════════════════════════
    // 第3组：剪贴板（Cut / Copy / Paste）
    // ══════════════════════════════════════════════════════════
    auto* aCut   = tb->addAction(makeLdIcon("cut"),   "Cut  [Ctrl+X]");
    auto* aCopy  = tb->addAction(makeLdIcon("copy"),  "Copy  [Ctrl+C]");
    auto* aPaste = tb->addAction(makeLdIcon("paste"), "Paste  [Ctrl+V]");
    aCut->setShortcut(QKeySequence::Cut);
    aCopy->setShortcut(QKeySequence::Copy);
    aPaste->setShortcut(QKeySequence::Paste);
    connect(aCut, &QAction::triggered, this, []{
        if (auto* w = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget()))
            w->cut();
    });
    connect(aCopy, &QAction::triggered, this, []{
        if (auto* w = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget()))
            w->copy();
    });
    connect(aPaste, &QAction::triggered, this, []{
        if (auto* w = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget()))
            w->paste();
    });
    tb->addSeparator();

    // ══════════════════════════════════════════════════════════
    // 第4组：编译（Build / Clean）
    // ══════════════════════════════════════════════════════════
    auto* aBuild = tb->addAction(makeLdIcon("build"), "Build / Compile  [Ctrl+B]");
    auto* aClean = tb->addAction(makeLdIcon("clean"), "Clean Build");
    connect(aBuild, &QAction::triggered, this, &MainWindow::buildProject);
    connect(aClean, &QAction::triggered, this, [this]{
        m_consoleEdit->clear();
        m_consoleEdit->appendPlainText("[ Clean ] Build output cleared.");
        m_consoleTabs->setCurrentWidget(m_consoleEdit);
        statusBar()->showMessage("Cleaned.", 2000);
    });
    aBuild->setShortcut(QKeySequence("Ctrl+B"));
    tb->addSeparator();

    // ══════════════════════════════════════════════════════════
    // 第5组：PLC 连接与控制（Connect · Transfer · Run · Stop）
    // ══════════════════════════════════════════════════════════
    m_aConnect  = tb->addAction(makeLdIcon("connect"),  "Connect to PLC  [Ctrl+D]");
    m_aTransfer = tb->addAction(makeLdIcon("download"), "Download Program to PLC");
    m_aRun  = tb->addAction(makeLdIcon("run"),  "Run PLC");
    m_aStop = tb->addAction(makeLdIcon("stop"), "Stop PLC");

    connect(m_aConnect,  &QAction::triggered, this, &MainWindow::connectToPlc);
    connect(m_aTransfer, &QAction::triggered, this, &MainWindow::downloadProject);
    connect(m_aRun, &QAction::triggered, this, [this]{
        if (m_connState != PlcConnState::Connected) {
            statusBar()->showMessage("Not connected to PLC.", 3000); return;
        }
        setPlcRunState(PlcRunState::Running);
        statusBar()->showMessage("PLC running.", 2000);
    });
    connect(m_aStop, &QAction::triggered, this, [this]{
        if (m_connState != PlcConnState::Connected) {
            statusBar()->showMessage("Not connected to PLC.", 3000); return;
        }
        setPlcRunState(PlcRunState::Stopped);
        statusBar()->showMessage("PLC stopped.", 2000);
    });

    m_aConnect->setShortcut(QKeySequence("Ctrl+D"));
    // 初始状态：Run/Stop 需要已连接；Transfer 下载对话框随时可用
    m_aRun->setEnabled(false);
    m_aStop->setEnabled(false);
    m_aTransfer->setEnabled(true);
    tb->addSeparator();

    // ══════════════════════════════════════════════════════════
    // 第6组：LD / FBD 编辑元件（互斥模式按钮）
    // 注意：这组工具只在激活图形视图（LD/FBD/SFC）时可见
    // ══════════════════════════════════════════════════════════

    // 前置分隔符也归入 m_ldToolActions，一起随视图类型显示/隐藏
    m_ldToolActions << tb->addSeparator();

    QActionGroup* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);

    struct ModeEntry {
        QString    iconType;
        QString    tooltip;
        EditorMode mode;
        bool       checked;
    };
    const QList<ModeEntry> ldTools = {
        { "select", "Select / Move  [Esc]",            Mode_Select,        true  },
        { "no",     "Normal Open Contact  -| |-",       Mode_AddContact_NO, false },
        { "nc",     "Normal Closed Contact  -|/|-",     Mode_AddContact_NC, false },
        { "pe",     "Rising Edge Contact  -|P|-",       Mode_AddContact_P,  false },
        { "ne",     "Falling Edge Contact  -|N|-",      Mode_AddContact_N,  false },
        { "coil",   "Output Coil  -( )-",               Mode_AddCoil,       false },
        { "set",    "Set Coil  -(S)-",                  Mode_AddCoil_S,     false },
        { "rst",    "Reset Coil  -(R)-",                Mode_AddCoil_R,     false },
        { "fb",     "Function Block",                   Mode_AddFuncBlock,  false },
        { "wire",   "Wire Connection",                  Mode_AddWire,       false },
    };

    for (const auto& e : ldTools) {
        // 各子组之间插入分隔符（分隔符也要归入隐藏列表）
        if (e.mode == Mode_AddContact_N || e.mode == Mode_AddCoil_R ||
            e.mode == Mode_AddFuncBlock)
            m_ldToolActions << tb->addSeparator();

        QAction* act = tb->addAction(makeLdIcon(e.iconType), e.tooltip);
        act->setCheckable(true);
        act->setChecked(e.checked);
        modeGroup->addAction(act);
        m_ldModeActions[e.mode] = act;
        m_ldToolActions << act;
        connect(act, &QAction::triggered, [this, e](){
            if (m_scene) m_scene->setMode(e.mode);
        });
    }

    // 初始状态：没有任何视图激活，先隐藏全部 LD 工具
    for (QAction* a : m_ldToolActions) a->setVisible(false);
}

// ──────────────────────────────────────────────────────────────
// 工具栏状态同步（Escape 等触发 modeChanged 时调用）
// ──────────────────────────────────────────────────────────────
void MainWindow::onLdModeChanged(EditorMode mode)
{
    for (auto it = m_ldModeActions.begin(); it != m_ldModeActions.end(); ++it)
        it.value()->setChecked(it.key() == mode);
}

// ============================================================
// 左侧停靠栏：项目树
// ============================================================
void MainWindow::setupProjectPanel()
{
    QDockWidget* dock = new QDockWidget("Project", this);
    dock->setObjectName("projectDock");
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);

    m_projectTree = new QTreeWidget();
    m_projectTree->setObjectName("projectTree");
    m_projectTree->setHeaderHidden(true);
    m_projectTree->setMinimumWidth(190);
    m_projectTree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_projectTree, &QTreeWidget::itemDoubleClicked,
            this, &MainWindow::onTreeDoubleClicked);
    connect(m_projectTree, &QTreeWidget::customContextMenuRequested,
            this, &MainWindow::onTreeContextMenu);

    m_projectTree->setStyle(new TreeBranchStyle());
    dock->setWidget(m_projectTree);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

// ============================================================
// 右侧停靠栏：函数库 + 调试器
// ============================================================
// Helper: recursively populate tree from <category>/<function>/<functionBlock> DOM
static void populateLibraryNode(QTreeWidgetItem* parent,
                                 const QDomElement& elem,
                                 const QIcon& folderIcon,
                                 const QIcon& fnIcon,
                                 const QIcon& fbIcon)
{
    for (QDomElement child = elem.firstChildElement();
         !child.isNull(); child = child.nextSiblingElement())
    {
        const QString tag = child.tagName();
        if (tag == "category") {
            auto* node = new QTreeWidgetItem(parent, QStringList{child.attribute("name")});
            node->setIcon(0, folderIcon);
            populateLibraryNode(node, child, folderIcon, fnIcon, fbIcon);
        } else if (tag == "function") {
            auto* node = new QTreeWidgetItem(parent, QStringList{child.attribute("name")});
            node->setIcon(0, fnIcon);
            const QString comment = child.attribute("comment");
            if (!comment.isEmpty()) node->setToolTip(0, comment);
            node->setData(0, Qt::UserRole, "function");
        } else if (tag == "functionBlock") {
            auto* node = new QTreeWidgetItem(parent, QStringList{child.attribute("name")});
            node->setIcon(0, fbIcon);
            const QString comment = child.attribute("comment");
            if (!comment.isEmpty()) node->setToolTip(0, comment);
            node->setData(0, Qt::UserRole, "functionBlock");
        }
    }
}

void MainWindow::setupLibraryPanel()
{
    QDockWidget* dock = new QDockWidget("Library", this);
    dock->setObjectName("libraryDock");
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);

    QTabWidget* libTabs = new QTabWidget();
    libTabs->setObjectName("libraryTabs");

    // Library 标签页
    QWidget* libWidget = new QWidget();
    QVBoxLayout* libLay = new QVBoxLayout(libWidget);
    libLay->setContentsMargins(4, 4, 4, 4);
    libLay->setSpacing(4);

    QLineEdit* searchEdit = new QLineEdit();
    searchEdit->setPlaceholderText("Search...");
    libLay->addWidget(searchEdit);

    m_libraryTree = new QTreeWidget();
    m_libraryTree->setObjectName("libraryTree");
    m_libraryTree->setHeaderHidden(true);
    m_libraryTree->setStyle(new TreeBranchStyle());

    const QIcon folderIcon(":/images/BLOCK.png");
    const QIcon fnIcon(":/images/BLOCK.png");
    const QIcon fbIcon(":/images/BLOCK.png");

    // Load library.xml from the application bundle or source conf directory
    // At runtime, look alongside the executable; fall back to source tree
    const QStringList searchPaths = {
        QCoreApplication::applicationDirPath() + "/conf/library.xml",
        QCoreApplication::applicationDirPath() + "/../Resources/conf/library.xml",
        QString(LIBRARY_XML_PATH)   // injected by CMake at compile time
    };

    QDomDocument libDoc;
    for (const QString& p : searchPaths) {
        QFile f(p);
        if (f.open(QFile::ReadOnly) && libDoc.setContent(&f))
            break;
        libDoc.clear();
    }

    if (!libDoc.isNull()) {
        QDomElement root = libDoc.documentElement(); // <library>
        for (QDomElement cat = root.firstChildElement("category");
             !cat.isNull(); cat = cat.nextSiblingElement("category"))
        {
            auto* topItem = new QTreeWidgetItem(m_libraryTree, QStringList{cat.attribute("name")});
            topItem->setIcon(0, folderIcon);
            populateLibraryNode(topItem, cat, folderIcon, fnIcon, fbIcon);
        }
    } else {
        // Fallback: minimal static list if XML not found
        const QStringList fallback = {
            "Standard Functions", "Standard Function Blocks", "Additional Function Blocks"
        };
        for (const QString& cat : fallback) {
            auto* item = new QTreeWidgetItem(m_libraryTree, QStringList{cat});
            item->setIcon(0, folderIcon);
        }
    }

    // User-defined POUs node (always present)
    {
        auto* userNode = new QTreeWidgetItem(m_libraryTree, QStringList{"User-defined POU"});
        userNode->setIcon(0, folderIcon);
    }

    libLay->addWidget(m_libraryTree);
    libTabs->addTab(libWidget, "Library");
    libTabs->addTab(new QWidget(), "Debugger");
    libTabs->tabBar()->setExpanding(false);  // 让标签页左对齐
    libTabs->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    dock->setWidget(libTabs);
    dock->setMinimumWidth(190);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

// ============================================================
// 底部停靠栏：Search | Console | PLC Log
// ============================================================
void MainWindow::setupConsolePanel()
{
    QDockWidget* dock = new QDockWidget("Console", this);
    dock->setObjectName("consoleDock");
    dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);

    m_consoleTabs = new QTabWidget();
    m_consoleTabs->setObjectName("consoleTabs");
    m_consoleTabs->tabBar()->setExpanding(false);  // 标签页左对齐，不拉伸填满

    // Search
    QWidget* searchWidget = new QWidget();
    QHBoxLayout* searchLay = new QHBoxLayout(searchWidget);
    searchLay->addWidget(new QLabel("Find:"));
    searchLay->addWidget(new QLineEdit(), 1);
    searchLay->addWidget(new QPushButton("Find"));
    searchLay->addStretch();
    m_consoleTabs->addTab(searchWidget, "Search");

    // Console
    m_consoleEdit = new QPlainTextEdit();
    m_consoleEdit->setObjectName("consoleEdit");
    m_consoleEdit->setReadOnly(true);
    m_consoleEdit->setFont(QFont("Courier New", 9));
    m_consoleTabs->addTab(m_consoleEdit, "Console");

    // PLC Log
    auto* logEdit = new QPlainTextEdit();
    logEdit->setReadOnly(true);
    logEdit->setFont(QFont("Courier New", 9));
    m_consoleTabs->addTab(logEdit, "PLC Log");

    dock->setWidget(m_consoleTabs);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    resizeDocks({dock}, {160}, Qt::Vertical);

    // 底部面板只占中央区域，不延伸到左/右停靠栏下方
    setCorner(Qt::BottomLeftCorner,  Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
}

// ============================================================
// 中央区域：MDI 多子窗口编辑区（标签页视图）
// ============================================================
void MainWindow::setupCentralArea()
{
    m_mdiArea = new QMdiArea();
    m_mdiArea->setObjectName("mdiArea");
    m_mdiArea->setViewMode(QMdiArea::TabbedView);
    m_mdiArea->setTabsClosable(true);
    m_mdiArea->setTabsMovable(true);
    m_mdiArea->setDocumentMode(true);  // 去掉子窗口边框，纯标签外观

    // 激活不同子窗口时：同步 m_scene，并根据视图类型显示/隐藏 LD 工具栏
    connect(m_mdiArea, &QMdiArea::subWindowActivated,
            this, [this](QMdiSubWindow* sw) {
        // 断开旧的 canUndo/canRedo 连接
        QObject::disconnect(m_undoConn);
        QObject::disconnect(m_redoConn);

        if (!sw) {
            m_scene = nullptr;
            for (QAction* a : m_ldToolActions) a->setVisible(false);
            m_aUndo->setEnabled(false);
            m_aRedo->setEnabled(false);
            return;
        }
        PouModel* pou = m_subWinPouMap.value(sw, nullptr);
        m_scene = pou ? m_sceneMap.value(pou, nullptr) : nullptr;

        // 图形语言（LD / FBD / SFC）显示 LD 工具栏，文本语言（ST / IL）隐藏
        const bool isGraphical = pou &&
            (pou->language == PouLanguage::LD  ||
             pou->language == PouLanguage::FBD ||
             pou->language == PouLanguage::SFC);
        for (QAction* a : m_ldToolActions) a->setVisible(isGraphical);

        // 根据视图类型连接 Undo/Redo 使能信号
        if (isGraphical && m_scene) {
            QUndoStack* us = m_scene->undoStack();
            m_aUndo->setEnabled(us->canUndo());
            m_aRedo->setEnabled(us->canRedo());
            m_undoConn = connect(us, &QUndoStack::canUndoChanged, m_aUndo, &QAction::setEnabled);
            m_redoConn = connect(us, &QUndoStack::canRedoChanged, m_aRedo, &QAction::setEnabled);
        } else {
            // 文本编辑器：始终启用（QPlainTextEdit 内建 undo/redo）
            m_aUndo->setEnabled(true);
            m_aRedo->setEnabled(true);
        }
    });

    setCentralWidget(m_mdiArea);
}

// ============================================================
// 重建项目树
// ============================================================
void MainWindow::rebuildProjectTree()
{
    m_projectTree->clear();

    if (!m_project) return;

    auto* root = new QTreeWidgetItem(m_projectTree,
                                      QStringList{m_project->projectName});
    root->setIcon(0, QIcon(":/images/PROJECT.png"));
    root->setData(0, Qt::UserRole, QString("project-root"));  // 标记根节点
    root->setExpanded(true);

    // 语言图标映射
    static const QMap<PouLanguage, QString> s_langIcon = {
        { PouLanguage::LD,  ":/images/LD.png"  },
        { PouLanguage::ST,  ":/images/ST.png"  },
        { PouLanguage::IL,  ":/images/IL.png"  },
        { PouLanguage::FBD, ":/images/FBD.png" },
        { PouLanguage::SFC, ":/images/SFC.png" },
    };

    auto addPouItem = [&](QTreeWidgetItem* parent, PouModel* pou) {
        auto* item = new QTreeWidgetItem(parent, QStringList{pou->name});
        item->setIcon(0, QIcon(s_langIcon.value(pou->language, ":/images/Unknown.png")));
        item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void*>(pou)));
    };

    // 三次遍历，按 Beremiz 惯例排列：Function → Function Blocks（分组）→ Program
    // Pass 1: Functions（直接挂根节点）
    for (PouModel* pou : m_project->pous)
        if (pou->pouType == PouType::Function)
            addPouItem(root, pou);

    // Pass 2: Function Blocks（放入可折叠子分组）
    QTreeWidgetItem* fbGroup = nullptr;
    for (PouModel* pou : m_project->pous) {
        if (pou->pouType != PouType::FunctionBlock) continue;
        if (!fbGroup) {
            fbGroup = new QTreeWidgetItem(root, QStringList{"Function Blocks"});
            fbGroup->setIcon(0, QIcon(":/images/FOLDER.png"));
            fbGroup->setExpanded(true);
        }
        addPouItem(fbGroup, pou);
    }

    // Pass 3: Programs（直接挂根节点）
    for (PouModel* pou : m_project->pous)
        if (pou->pouType == PouType::Program)
            addPouItem(root, pou);

    m_projectTree->expandAll();
}

// ============================================================
// 双击树节点 → 打开对应标签页
// ============================================================
void MainWindow::onTreeDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;

    // 项目根节点 → 打开项目属性面板
    if (item->data(0, Qt::UserRole).toString() == "project-root") {
        openProjectProperties();
        return;
    }

    void* ptr = item->data(0, Qt::UserRole).value<void*>();
    if (!ptr) return;
    openPouTab(static_cast<PouModel*>(ptr));
}

// ============================================================
// 右键菜单 → 添加 POU
// ============================================================
void MainWindow::onTreeContextMenu(const QPoint& pos)
{
    QMenu menu(this);
    QAction* addAct = menu.addAction("Add POU...");
    if (menu.exec(m_projectTree->viewport()->mapToGlobal(pos)) != addAct)
        return;

    // 对话框：输入名称 / 选择类型 / 选择语言
    QDialog dlg(this);
    dlg.setWindowTitle("New POU");
    dlg.setFixedWidth(300);

    QFormLayout form(&dlg);

    QLineEdit nameEdit;
    nameEdit.setPlaceholderText("e.g. MyCounter");
    form.addRow("Name:", &nameEdit);

    QComboBox typeCombo;
    typeCombo.addItems({"Function Block", "Program", "Function"});
    form.addRow("Type:", &typeCombo);

    QComboBox langCombo;
    langCombo.addItems({"LD", "ST", "IL", "FBD", "SFC"});
    form.addRow("Language:", &langCombo);

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                              Qt::Horizontal, &dlg);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    const QString name = nameEdit.text().trimmed();
    if (name.isEmpty()) return;
    if (m_project->pouNameExists(name)) {
        QMessageBox::warning(this, "Error",
            QString("A POU named \"%1\" already exists.").arg(name));
        return;
    }

    const QStringList typeStr = {"functionBlock", "program", "function"};
    PouType  type = PouModel::typeFromString(typeStr[typeCombo.currentIndex()]);
    PouLanguage lang = PouModel::langFromString(langCombo.currentText());

    PouModel* pou = m_project->addPou(name, type, lang);
    rebuildProjectTree();
    openPouTab(pou);
}

// ============================================================
// 打开 / 切换到某个 POU 的子窗口
// ============================================================
void MainWindow::openPouTab(PouModel* pou)
{
    if (!pou) return;

    // 如果已经打开，直接激活
    for (QMdiSubWindow* sw : m_mdiArea->subWindowList()) {
        if (m_subWinPouMap.value(sw) == pou) {
            m_mdiArea->setActiveSubWindow(sw);
            return;
        }
    }

    // 新建子窗口
    QWidget* editor = createPouEditorWidget(pou);

    QMdiSubWindow* sw = m_mdiArea->addSubWindow(editor);
    sw->setAttribute(Qt::WA_DeleteOnClose);
    sw->setWindowTitle(QString("[%1]  %2")
        .arg(PouModel::langToString(pou->language))
        .arg(pou->name));

    m_subWinPouMap[sw] = pou;

    // 子窗口销毁时清理映射
    connect(sw, &QObject::destroyed, this, [this, sw]() {
        m_subWinPouMap.remove(sw);
    });

    sw->show();
}

// ============================================================
// 关闭所有 POU 子窗口（新建 / 打开项目时调用）
// ============================================================
void MainWindow::closeAllPouTabs()
{
    // 先断开 destroyed 信号，避免在 closeAllSubWindows 过程中迭代器失效
    for (QMdiSubWindow* sw : m_mdiArea->subWindowList())
        sw->disconnect(this);

    m_subWinPouMap.clear();
    m_scene = nullptr;
    m_mdiArea->closeAllSubWindows();
}

// ============================================================
// 项目属性面板（单例）
// ============================================================
void MainWindow::openProjectProperties()
{
    if (!m_project) return;

    // 若已打开则激活
    if (m_projPropSubWin) {
        m_mdiArea->setActiveSubWindow(m_projPropSubWin);
        return;
    }

    QWidget* w = createProjectPropertiesWidget();
    m_projPropSubWin = m_mdiArea->addSubWindow(w);
    m_projPropSubWin->setWindowTitle(
        QString("Project — %1").arg(m_project->projectName));
    m_projPropSubWin->setWindowIcon(QIcon(":/images/PROJECT.png"));
    m_projPropSubWin->setAttribute(Qt::WA_DeleteOnClose);
    m_projPropSubWin->resize(480, 560);
    m_projPropSubWin->show();

    // 关闭时清空指针
    connect(m_projPropSubWin, &QObject::destroyed, this,
            [this]{ m_projPropSubWin = nullptr; });
}

QWidget* MainWindow::createProjectPropertiesWidget()
{
    auto* w      = new QWidget();
    auto* topLay = new QVBoxLayout(w);
    topLay->setContentsMargins(12, 12, 12, 12);
    topLay->setSpacing(10);

    // ── Project Properties ────────────────────────────────────
    auto* projGroup = new QGroupBox("Project Properties");
    auto* projForm  = new QFormLayout(projGroup);
    projForm->setContentsMargins(8, 10, 8, 10);
    projForm->setSpacing(6);

    auto* nameEdit    = new QLineEdit(m_project->projectName);
    auto* authorEdit  = new QLineEdit(m_project->author);
    auto* compEdit    = new QLineEdit(m_project->companyName);
    auto* verEdit     = new QLineEdit(m_project->productVersion);
    auto* descEdit    = new QPlainTextEdit(m_project->description);
    descEdit->setFixedHeight(68);
    auto* createdLbl  = new QLabel(m_project->creationDateTime.isEmpty()
                                    ? "(unknown)" : m_project->creationDateTime);
    auto* modLbl      = new QLabel(m_project->modificationDateTime.isEmpty()
                                    ? "(unknown)" : m_project->modificationDateTime);
    createdLbl->setStyleSheet("color:#666;");
    modLbl->setStyleSheet("color:#666;");

    projForm->addRow("Project Name:",  nameEdit);
    projForm->addRow("Author:",        authorEdit);
    projForm->addRow("Company:",       compEdit);
    projForm->addRow("Version:",       verEdit);
    projForm->addRow("Description:",   descEdit);
    projForm->addRow("Created:",       createdLbl);
    projForm->addRow("Last Modified:", modLbl);

    // ── Build ────────────────────────────────────────────────
    auto* buildGroup = new QGroupBox("Build");
    auto* buildForm  = new QFormLayout(buildGroup);
    buildForm->setContentsMargins(8, 10, 8, 10);
    buildForm->setSpacing(6);

    auto* targetCombo  = new QComboBox();
    targetCombo->addItems({"Linux", "Mac", "Windows", "Embedded"});
    targetCombo->setCurrentText(m_project->targetType);
    auto* compilerEdit = new QLineEdit(m_project->compiler);
    auto* cflagsEdit   = new QLineEdit(m_project->cflags);
    auto* linkerEdit   = new QLineEdit(m_project->linker);
    auto* ldflagsEdit  = new QLineEdit(m_project->ldflags);

    buildForm->addRow("Target Type:", targetCombo);
    buildForm->addRow("Compiler:",    compilerEdit);
    buildForm->addRow("CFLAGS:",      cflagsEdit);
    buildForm->addRow("Linker:",      linkerEdit);
    buildForm->addRow("LDFLAGS:",     ldflagsEdit);

    topLay->addWidget(projGroup);
    topLay->addWidget(buildGroup);
    topLay->addStretch();

    // ── 连接：字段变化 → 更新 ProjectModel ───────────────────
    connect(nameEdit, &QLineEdit::textChanged, this, [this](const QString& v) {
        m_project->projectName = v;
        m_project->markDirty();
        updateWindowTitle();
        if (m_projPropSubWin)
            m_projPropSubWin->setWindowTitle(QString("Project — %1").arg(v));
        // 同步项目树根节点文字
        if (m_projectTree->topLevelItemCount() > 0)
            m_projectTree->topLevelItem(0)->setText(0, v);
    });
    connect(authorEdit,  &QLineEdit::textChanged, this,
            [this](const QString& v){ m_project->author = v; m_project->markDirty(); });
    connect(compEdit,    &QLineEdit::textChanged, this,
            [this](const QString& v){ m_project->companyName = v; m_project->markDirty(); });
    connect(verEdit,     &QLineEdit::textChanged, this,
            [this](const QString& v){ m_project->productVersion = v; m_project->markDirty(); });
    connect(descEdit, &QPlainTextEdit::textChanged, this, [this, descEdit]{
        m_project->description = descEdit->toPlainText();
        m_project->markDirty();
    });
    connect(targetCombo, &QComboBox::currentTextChanged, this,
            [this](const QString& v){ m_project->targetType = v; m_project->markDirty(); });
    connect(compilerEdit, &QLineEdit::textChanged, this,
            [this](const QString& v){ m_project->compiler = v; m_project->markDirty(); });
    connect(cflagsEdit,  &QLineEdit::textChanged, this,
            [this](const QString& v){ m_project->cflags = v; m_project->markDirty(); });
    connect(linkerEdit,  &QLineEdit::textChanged, this,
            [this](const QString& v){ m_project->linker = v; m_project->markDirty(); });
    connect(ldflagsEdit, &QLineEdit::textChanged, this,
            [this](const QString& v){ m_project->ldflags = v; m_project->markDirty(); });

    return w;
}

// ============================================================
// 创建 POU 编辑器控件
// ============================================================
QWidget* MainWindow::createPouEditorWidget(PouModel* pou)
{
    QWidget* w = new QWidget();
    QVBoxLayout* vlay = new QVBoxLayout(w);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);

    QWidget* varDecl = createVarDeclWidget(pou);

    QWidget* editorArea = nullptr;

    const bool isGraphical = (pou->language == PouLanguage::LD  ||
                               pou->language == PouLanguage::FBD ||
                               pou->language == PouLanguage::SFC ||
                               !pou->graphicalXml.isEmpty());

    if (isGraphical) {
        // ── 统一图形编辑器（LD / FBD / SFC，含 PLCopen 导入）──────
        PlcOpenViewer* scene = m_sceneMap.value(pou, nullptr);
        if (!scene) {
            scene = new PlcOpenViewer(this);
            if (!pou->graphicalXml.isEmpty())
                scene->loadFromXmlString(pou->graphicalXml);
            else {
                // 传入语言字符串，scene 据此初始化正确的 body DOM
                QString langStr = "LD";
                if (pou->language == PouLanguage::FBD) langStr = "FBD";
                else if (pou->language == PouLanguage::SFC) langStr = "SFC";
                scene->initEmpty(langStr);
            }
            m_sceneMap[pou] = scene;
        }

        auto* view = new LadderView();
        view->setScene(scene);

        if (!m_scene) m_scene = scene;

        // 延迟 fitInView，等 MDI 子窗口完成布局后再缩放（0ms 有时不够）
        QTimer::singleShot(50, view, [view, scene](){
            QRectF r = scene->itemsBoundingRect().adjusted(-40, -40, 40, 40);
            if (r.isEmpty())
                r = QRectF(0, 0, 800, 600);
            // 确保 sceneRect 覆盖全部内容（防止大型图形被裁剪）
            if (!scene->sceneRect().contains(r))
                scene->setSceneRect(r.adjusted(-40, -40, 40, 40));
            view->fitInView(r, Qt::KeepAspectRatio);
        });

        connect(scene, &PlcOpenViewer::modeChanged,
                this,  &MainWindow::onLdModeChanged);
        connect(scene, &PlcOpenViewer::modeChanged,
                view,  &LadderView::onModeChanged);

        editorArea = view;

    } else if (pou->language == PouLanguage::ST ||
               pou->language == PouLanguage::IL) {
        // ── ST / IL 文本编辑器 ───────────────────────────────────
        auto* editor = new QPlainTextEdit();
        editor->setObjectName("stEditor");
        editor->setPlainText(pou->code);
        editor->setFont(QFont("Courier New", 11));

        new StHighlighter(editor->document());

        connect(editor, &QPlainTextEdit::textChanged, [pou, editor](){
            pou->code = editor->toPlainText();
        });
        editorArea = editor;

    } else {
        // ── 其他占位 ──────────────────────────────────────────────
        auto* placeholder = new QLabel(
            QString("[ %1 editor — coming soon ]")
                .arg(PouModel::langToString(pou->language)));
        placeholder->setAlignment(Qt::AlignCenter);
        editorArea = placeholder;
    }

    QSplitter* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(varDecl);
    splitter->addWidget(editorArea);
    splitter->setSizes({160, 600});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    vlay->addWidget(splitter);
    return w;
}

// ============================================================
// 变量声明表控件（由 PouModel 填充）
// ============================================================
QWidget* MainWindow::createVarDeclWidget(PouModel* pou)
{
    QWidget* w = new QWidget();
    w->setObjectName("varDeclWidget");
    QVBoxLayout* vlay = new QVBoxLayout(w);
    vlay->setContentsMargins(6, 4, 6, 0);
    vlay->setSpacing(4);

    // — 第一行：Description + Class Filter + 按钮 —
    QHBoxLayout* hlay = new QHBoxLayout();
    hlay->setSpacing(6);
    hlay->addWidget(new QLabel("Description:"));
    auto* descEdit = new QLineEdit(pou->description);
    hlay->addWidget(descEdit, 1);
    hlay->addSpacing(16);
    hlay->addWidget(new QLabel("Class Filter:"));
    auto* classFilter = new QComboBox();
    classFilter->addItems({"All", "Input", "Output", "InOut", "Local", "External"});
    classFilter->setFixedWidth(80);
    hlay->addWidget(classFilter);
    auto* btnAdd = new QPushButton("+");
    btnAdd->setObjectName("btnVarAdd");
    btnAdd->setFixedSize(22, 22);
    auto* btnDel = new QPushButton("-");
    btnDel->setObjectName("btnVarDel");
    btnDel->setFixedSize(22, 22);
    hlay->addWidget(btnAdd);
    hlay->addWidget(btnDel);
    vlay->addLayout(hlay);

    // — 变量表格 —
    auto* table = new QTableWidget();
    table->setObjectName("varTable");
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({"#", "Name", "Class", "Type", "Initial Value", "Comment"});
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    table->setColumnWidth(0, 32);
    table->setColumnWidth(1, 110);
    table->setColumnWidth(2, 75);
    table->setColumnWidth(3, 75);
    table->setColumnWidth(4, 100);

    // —— 辅助：将 pou->variables[row] 的数据写入 table 第 row 行 ——
    auto fillRow = [table](int row, const VariableDecl& v) {
        table->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        table->setItem(row, 1, new QTableWidgetItem(v.name));
        table->setItem(row, 2, new QTableWidgetItem(v.varClass));
        table->setItem(row, 3, new QTableWidgetItem(v.type));
        table->setItem(row, 4, new QTableWidgetItem(v.initValue));
        table->setItem(row, 5, new QTableWidgetItem(v.comment));
        table->setRowHeight(row, 20);
        // 第0列（序号）不可编辑
        if (auto* it = table->item(row, 0))
            it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    };

    // —— 辅助：刷新整列序号 ——
    auto refreshNumbers = [table]() {
        for (int r = 0; r < table->rowCount(); ++r) {
            if (!table->item(r, 0))
                table->setItem(r, 0, new QTableWidgetItem());
            table->item(r, 0)->setText(QString::number(r + 1));
        }
    };

    // 从 PouModel 填充（阻塞 cellChanged，避免在初始化时触发同步）
    table->blockSignals(true);
    const int rowCount = pou->variables.size();
    table->setRowCount(rowCount);
    for (int i = 0; i < rowCount; ++i)
        fillRow(i, pou->variables[i]);
    table->blockSignals(false);

    // —— + 按钮：追加一行空变量 ——
    connect(btnAdd, &QPushButton::clicked, table, [table, pou, fillRow, refreshNumbers]() {
        VariableDecl v;
        v.name     = QString("var%1").arg(pou->variables.size() + 1);
        v.varClass = "Local";
        v.type     = "BOOL";
        pou->variables.append(v);

        table->blockSignals(true);
        int row = table->rowCount();
        table->insertRow(row);
        fillRow(row, v);
        refreshNumbers();
        table->blockSignals(false);

        table->scrollToBottom();
        table->selectRow(row);
    });

    // —— - 按钮：删除选中行 ——
    connect(btnDel, &QPushButton::clicked, table, [table, pou, refreshNumbers]() {
        QList<int> rows;
        for (auto* sel : table->selectedItems())
            if (!rows.contains(sel->row())) rows.append(sel->row());
        std::sort(rows.rbegin(), rows.rend());   // 从大到小删，避免索引漂移

        table->blockSignals(true);
        for (int r : rows) {
            if (r >= 0 && r < pou->variables.size()) {
                pou->variables.removeAt(r);
                table->removeRow(r);
            }
        }
        refreshNumbers();
        table->blockSignals(false);
    });

    // —— cellChanged：将编辑同步回 PouModel ——
    connect(table, &QTableWidget::cellChanged, [table, pou](int row, int col) {
        if (row < 0 || row >= pou->variables.size()) return;
        auto* it = table->item(row, col);
        if (!it) return;
        VariableDecl& v = pou->variables[row];
        switch (col) {
        case 1: v.name      = it->text(); break;
        case 2: v.varClass  = it->text(); break;
        case 3: v.type      = it->text(); break;
        case 4: v.initValue = it->text(); break;
        case 5: v.comment   = it->text(); break;
        default: break;
        }
    });

    vlay->addWidget(table);
    return w;
}

// ============================================================
// 项目操作
// ============================================================
void MainWindow::newProject()
{
    if (m_project && m_project->isDirty()) {
        int ret = QMessageBox::question(this, "New Project",
            "Current project has unsaved changes. Discard them?",
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    const QString name = QInputDialog::getText(
        this, "New Project", "Project name:", QLineEdit::Normal, "Untitled");
    if (name.trimmed().isEmpty()) return;

    // 清空现有子窗口
    closeAllPouTabs();
    m_sceneMap.clear();

    delete m_project;
    m_project = new ProjectModel(this);
    connect(m_project, &ProjectModel::changed, this, &MainWindow::updateWindowTitle);
    m_project->projectName = name.trimmed();

    // 默认创建一个 LD 程序
    PouModel* pou = m_project->addPou("main", PouType::Program, PouLanguage::LD);
    m_project->clearDirty();

    rebuildProjectTree();
    openPouTab(pou);
    updateWindowTitle();
}

void MainWindow::openProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Open Project", QString(),
        "TiZi Project (*.tizi);;XML Files (*.xml);;All Files (*)");
    if (path.isEmpty()) return;

    // 清空现有子窗口
    closeAllPouTabs();
    m_sceneMap.clear();

    delete m_project;
    m_project = new ProjectModel(this);
    connect(m_project, &ProjectModel::changed, this, &MainWindow::updateWindowTitle);

    if (!m_project->loadFromFile(path)) {
        QMessageBox::critical(this, "Open Error",
            QString("Failed to open:\n%1").arg(path));
        delete m_project;
        m_project = nullptr;
        return;
    }

    rebuildProjectTree();
    if (!m_project->pous.isEmpty())
        openPouTab(m_project->pous.first());

    updateWindowTitle();
    statusBar()->showMessage(QString("Opened: %1").arg(path), 3000);
}

// ── 保存前将所有打开的图形场景同步到 PouModel ─────────────────
static void syncScenesBeforeSave(const QMap<PouModel*, PlcOpenViewer*>& sceneMap)
{
    for (auto it = sceneMap.cbegin(); it != sceneMap.cend(); ++it) {
        PouModel*      pou   = it.key();
        PlcOpenViewer* scene = it.value();
        if (!scene) continue;
        QString xml = scene->toXmlString();
        if (!xml.isEmpty())
            pou->graphicalXml = xml;
    }
}

void MainWindow::saveProject()
{
    if (!m_project) return;
    if (m_project->filePath.isEmpty()) {
        saveProjectAs();
        return;
    }
    syncScenesBeforeSave(m_sceneMap);
    if (!m_project->saveToFile(m_project->filePath)) {
        QMessageBox::critical(this, "Save Error",
            QString("Failed to save:\n%1").arg(m_project->filePath));
        return;
    }
    updateWindowTitle();
    statusBar()->showMessage("Saved.", 3000);
}

void MainWindow::saveProjectAs()
{
    if (!m_project) return;
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Project As", m_project->projectName + ".tizi",
        "TiZi Project (*.tizi);;XML Files (*.xml);;All Files (*)");
    if (path.isEmpty()) return;

    syncScenesBeforeSave(m_sceneMap);
    if (!m_project->saveToFile(path)) {
        QMessageBox::critical(this, "Save Error",
            QString("Failed to save:\n%1").arg(path));
        return;
    }
    updateWindowTitle();
    statusBar()->showMessage(QString("Saved: %1").arg(path), 3000);
}

// ============================================================
// 编译：将整个项目 PLCopen XML 转换为 ST 中间代码，显示到控制台
// ============================================================
void MainWindow::buildProject()
{
    if (!m_project) {
        m_consoleEdit->appendPlainText("[ Build ] No project loaded.");
        m_consoleTabs->setCurrentWidget(m_consoleEdit);
        return;
    }

    m_consoleEdit->clear();
    m_consoleEdit->appendPlainText(
        QString("[ Build ] Converting project \"%1\" to ST ...").arg(m_project->projectName));

    // 若项目尚未保存，提示先保存
    if (m_project->filePath.isEmpty()) {
        m_consoleEdit->appendPlainText(
            "[ Build ] Project not saved to file yet. Please save first (Ctrl+S).");
        m_consoleTabs->setCurrentWidget(m_consoleEdit);
        statusBar()->showMessage("Build failed: unsaved project.", 4000);
        return;
    }

    // 读取已保存的 XML 文件
    QString xmlContent;
    {
        QFile f(m_project->filePath);
        if (f.open(QFile::ReadOnly | QFile::Text))
            xmlContent = QString::fromUtf8(f.readAll());
    }

    if (xmlContent.isEmpty()) {
        m_consoleEdit->appendPlainText("[ Build ] Cannot read project file.");
        m_consoleTabs->setCurrentWidget(m_consoleEdit);
        statusBar()->showMessage("Build failed.", 4000);
        return;
    }

    QString stCode = StGenerator::fromXml(xmlContent);
    if (stCode.isEmpty()) {
        m_consoleEdit->appendPlainText(
            "[ Build ] Error: " + StGenerator::lastError());
        m_consoleTabs->setCurrentWidget(m_consoleEdit);
        statusBar()->showMessage("Build failed.", 4000);
        return;
    }

    m_consoleEdit->appendPlainText("[ Build ] Done — ST output:\n");
    m_consoleEdit->appendPlainText("─────────────────────────────────────────");
    m_consoleEdit->appendPlainText(stCode);
    m_consoleTabs->setCurrentWidget(m_consoleEdit);

    statusBar()->showMessage("Build complete.", 4000);
}

// ============================================================
// 下载：打开 DownloadDialog
// ============================================================
void MainWindow::downloadProject()
{
    DownloadDialog dlg(this);
    dlg.exec();
}

// ============================================================
// 状态栏：PLC 连接状态指示器
// ============================================================

// 生成圆形 LED 的 QLabel stylesheet
QString MainWindow::ledStyle(const QString& color)
{
    return QString(
        "background-color: %1;"
        "border-radius: 6px;"
        "border: 1px solid rgba(0,0,0,0.25);"
    ).arg(color);
}

void MainWindow::setupStatusBar()
{
    // ── 连接状态区（左侧组）──────────────────────────────────
    auto* connFrame = new QFrame();
    connFrame->setFrameShape(QFrame::StyledPanel);
    connFrame->setContentsMargins(0, 0, 0, 0);
    auto* connLay = new QHBoxLayout(connFrame);
    connLay->setContentsMargins(6, 1, 6, 1);
    connLay->setSpacing(5);

    // 小图标（插头）
    auto* connIcon = new QLabel();
    connIcon->setPixmap(QPixmap(":/images/Connect.png")
                            .scaled(14, 14, Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation));
    connLay->addWidget(connIcon);

    // LED
    m_connLed = new QLabel();
    m_connLed->setFixedSize(12, 12);
    m_connLed->setStyleSheet(ledStyle("#888888"));
    connLay->addWidget(m_connLed);

    // 文字
    m_connLabel = new QLabel("Disconnected");
    m_connLabel->setMinimumWidth(100);
    connLay->addWidget(m_connLabel);

    // PLC URI（连接后显示）
    m_uriLabel = new QLabel();
    m_uriLabel->setStyleSheet("color: #666666; font-size: 10px;");
    m_uriLabel->hide();
    connLay->addWidget(m_uriLabel);

    // 点击整个区域 → 连接/断开对话框
    connFrame->setCursor(Qt::PointingHandCursor);
    connFrame->installEventFilter(this);
    connFrame->setProperty("plcConnFrame", true);

    // ── 分隔线 ──────────────────────────────────────────────
    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);

    // ── 运行状态区 ──────────────────────────────────────────
    auto* stateFrame = new QFrame();
    stateFrame->setFrameShape(QFrame::StyledPanel);
    auto* stateLay = new QHBoxLayout(stateFrame);
    stateLay->setContentsMargins(6, 1, 6, 1);
    stateLay->setSpacing(5);

    auto* stateIcon = new QLabel("PLC:");
    stateIcon->setStyleSheet("font-weight: bold; font-size: 10px;");
    stateLay->addWidget(stateIcon);

    m_stateLed = new QLabel();
    m_stateLed->setFixedSize(12, 12);
    m_stateLed->setStyleSheet(ledStyle("#888888"));
    stateLay->addWidget(m_stateLed);

    m_stateLabel = new QLabel("Unknown");
    m_stateLabel->setMinimumWidth(70);
    stateLay->addWidget(m_stateLabel);

    // ── 添加到状态栏（永久控件显示在右侧）──────────────────
    statusBar()->addPermanentWidget(connFrame);
    statusBar()->addPermanentWidget(sep);
    statusBar()->addPermanentWidget(stateFrame);

    // 初始状态
    setPlcConnState(PlcConnState::Disconnected);
    setPlcRunState(PlcRunState::Unknown);
}

// ── 更新连接状态 LED + 文字 ───────────────────────────────────
void MainWindow::setPlcConnState(PlcConnState state)
{
    m_connState = state;
    struct { const char* color; const char* text; } cfg[] = {
        { "#888888", "Disconnected" },   // Disconnected
        { "#FFC107", "Connecting…"  },   // Connecting
        { "#4CAF50", "Connected"    },   // Connected
    };
    int i = static_cast<int>(state);
    m_connLed->setStyleSheet(ledStyle(cfg[i].color));
    m_connLabel->setText(cfg[i].text);

    // URI 仅在已连接时显示
    if (state == PlcConnState::Connected && !m_plcUri.isEmpty()) {
        m_uriLabel->setText(m_plcUri);
        m_uriLabel->show();
    } else {
        m_uriLabel->hide();
    }

    // 工具栏按钮：已连接时切换图标 + 启用 Run/Stop/Transfer
    const bool connected = (state == PlcConnState::Connected);
    if (m_aConnect) {
        m_aConnect->setIcon(makeLdIcon(connected ? "disconnect" : "connect"));
        m_aConnect->setToolTip(connected
            ? "Disconnect from PLC  [Ctrl+D]"
            : "Connect to PLC  [Ctrl+D]");
    }
    if (m_aRun)  m_aRun->setEnabled(connected);
    if (m_aStop) m_aStop->setEnabled(connected);

    // 未连接时重置运行状态
    if (state == PlcConnState::Disconnected)
        setPlcRunState(PlcRunState::Unknown);
}

// ── 更新运行状态 LED + 文字 ───────────────────────────────────
void MainWindow::setPlcRunState(PlcRunState state)
{
    m_runState = state;
    struct { const char* color; const char* text; } cfg[] = {
        { "#888888", "Unknown" },   // Unknown
        { "#FF7043", "Stopped" },   // Stopped
        { "#4CAF50", "Running" },   // Running
        { "#FFC107", "Paused"  },   // Paused
        { "#F44336", "Error"   },   // Error
    };
    int i = static_cast<int>(state);
    m_stateLed->setStyleSheet(ledStyle(cfg[i].color));
    m_stateLabel->setText(cfg[i].text);
}

// ── 过滤事件：点击连接区域 → 打开连接对话框 ──────────────────
bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj->property("plcConnFrame").toBool() &&
        ev->type() == QEvent::MouseButtonRelease)
    {
        connectToPlc();
        return true;
    }
    return QMainWindow::eventFilter(obj, ev);
}

// ── 连接 / 断开 PLC 对话框（存根，待实现真正协议）─────────────
void MainWindow::connectToPlc()
{
    if (m_connState == PlcConnState::Connected) {
        // 已连接 → 断开
        int ret = QMessageBox::question(
            this, "Disconnect",
            QString("Disconnect from %1?").arg(
                m_plcUri.isEmpty() ? "PLC" : m_plcUri),
            QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            m_plcUri.clear();
            setPlcConnState(PlcConnState::Disconnected);
            setPlcRunState(PlcRunState::Unknown);
            statusBar()->showMessage("Disconnected from PLC.", 3000);
        }
        return;
    }

    // 未连接 → 弹出连接对话框
    QDialog dlg(this);
    dlg.setWindowTitle("Connect to PLC");
    dlg.setFixedWidth(340);

    QFormLayout form(&dlg);

    auto* uriEdit = new QLineEdit(
        m_plcUri.isEmpty() ? "PYRO://localhost:61131" : m_plcUri);
    form.addRow("PLC URI:", uriEdit);

    QDialogButtonBox btns(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                          Qt::Horizontal, &dlg);
    form.addRow(&btns);
    connect(&btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(&btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;
    const QString uri = uriEdit->text().trimmed();
    if (uri.isEmpty()) return;

    m_plcUri = uri;

    // 模拟连接过程（真实协议在此接入）
    setPlcConnState(PlcConnState::Connecting);
    statusBar()->showMessage(QString("Connecting to %1…").arg(uri), 2000);

    // 延迟 800ms 模拟握手，然后显示已连接
    // （真实实现中：在此发起网络连接，回调时更新状态）
    QTimer::singleShot(800, this, [this]() {
        setPlcConnState(PlcConnState::Connected);
        setPlcRunState(PlcRunState::Stopped);
        statusBar()->showMessage(
            QString("Connected to %1").arg(m_plcUri), 3000);
    });
}

// ============================================================
// 视图缩放
// ============================================================

// 从当前活跃的 MDI 子窗口中找到 LadderView
LadderView* MainWindow::activeView() const
{
    QMdiSubWindow* sw = m_mdiArea ? m_mdiArea->activeSubWindow() : nullptr;
    if (!sw) return nullptr;
    return sw->findChild<LadderView*>();
}

void MainWindow::zoomIn()
{
    if (LadderView* v = activeView()) {
        // 限制最大缩放 5×
        if (v->transform().m11() < 5.0)
            v->scale(1.25, 1.25);
    }
}

void MainWindow::zoomOut()
{
    if (LadderView* v = activeView()) {
        // 限制最小缩放 0.05×
        if (v->transform().m11() > 0.05)
            v->scale(1.0 / 1.25, 1.0 / 1.25);
    }
}

void MainWindow::zoomFit()
{
    LadderView* v = activeView();
    if (!v || !v->scene()) return;

    QRectF r = v->scene()->itemsBoundingRect().adjusted(-40, -40, 40, 40);
    if (r.isEmpty()) r = QRectF(0, 0, 800, 600);
    v->fitInView(r, Qt::KeepAspectRatio);
}

// ============================================================
// 窗口标题：项目名 [*] — TiZi
// ============================================================
void MainWindow::updateWindowTitle()
{
    if (!m_project) {
        setWindowTitle("TiZi PLC Editor");
        return;
    }
    const QString dirty = m_project->isDirty() ? " *" : "";
    setWindowTitle(QString("%1%2 — TiZi PLC Editor")
                       .arg(m_project->projectName)
                       .arg(dirty));
}

// ============================================================
// 主题切换
// ============================================================
void MainWindow::applyTheme(const QString& qrcPath)
{
    QFile f(qrcPath);
    if (!f.open(QFile::ReadOnly)) return;

    qApp->setStyleSheet(QString::fromUtf8(f.readAll()));
    m_currentTheme = qrcPath;

    // 同步 QPalette —— macOS 上 QTreeWidget viewport 直接读 palette，不读 QSS
    const bool dark = qrcPath.contains("dark_theme");
    QPalette pal = qApp->palette();
    if (dark) {
        pal.setColor(QPalette::Window,          QColor("#1E1E1E"));
        pal.setColor(QPalette::WindowText,      QColor("#D4D4D4"));
        pal.setColor(QPalette::Base,            QColor("#252526"));
        pal.setColor(QPalette::AlternateBase,   QColor("#1E1E1E"));
        pal.setColor(QPalette::Text,            QColor("#D4D4D4"));
        pal.setColor(QPalette::Button,          QColor("#3C3C3C"));
        pal.setColor(QPalette::ButtonText,      QColor("#D4D4D4"));
        pal.setColor(QPalette::Highlight,       QColor("#094771"));
        pal.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
        pal.setColor(QPalette::ToolTipBase,     QColor("#252526"));
        pal.setColor(QPalette::ToolTipText,     QColor("#D4D4D4"));
        pal.setColor(QPalette::PlaceholderText, QColor("#6E6E6E"));
    } else {
        pal.setColor(QPalette::Window,          QColor("#F0F0F0"));
        pal.setColor(QPalette::WindowText,      QColor("#1A1A1A"));
        pal.setColor(QPalette::Base,            QColor("#FFFFFF"));
        pal.setColor(QPalette::AlternateBase,   QColor("#F7F9FC"));
        pal.setColor(QPalette::Text,            QColor("#1A1A1A"));
        pal.setColor(QPalette::Button,          QColor("#E8E8E8"));
        pal.setColor(QPalette::ButtonText,      QColor("#1A1A1A"));
        pal.setColor(QPalette::Highlight,       QColor("#0078D7"));
        pal.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
        pal.setColor(QPalette::ToolTipBase,     QColor("#FFFFFF"));
        pal.setColor(QPalette::ToolTipText,     QColor("#1A1A1A"));
        pal.setColor(QPalette::PlaceholderText, QColor("#999999"));
    }
    qApp->setPalette(pal);

    // 强制所有顶层及子控件刷新 palette（macOS 有时需要显式通知）
    for (QWidget* w : qApp->allWidgets())
        w->setPalette(pal);

    // 通知所有已打开的图形场景重绘背景
    for (PlcOpenViewer* scene : m_sceneMap)
        scene->update();
}
