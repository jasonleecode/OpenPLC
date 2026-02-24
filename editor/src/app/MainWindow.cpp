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
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFont>
#include <QFile>
#include <QMenu>
#include <QTimer>
#include <QFrame>
#include <QEvent>
#include <QApplication>

#include "../editor/scene/LadderScene.h"
#include "../editor/scene/LadderView.h"
#include "../editor/scene/PlcOpenViewer.h"
#include "../utils/StHighlighter.h"
#include "../core/compiler/CodeGenerator.h"

// PlcOpenViewer 兼作所有图形语言（LD/FBD/SFC）的统一编辑器

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
    QFile styleFile(":/dark_theme.qss");
    if (styleFile.open(QFile::ReadOnly))
        qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    // 创建默认项目
    m_project = new ProjectModel(this);
    connect(m_project, &ProjectModel::changed, this, &MainWindow::updateWindowTitle);
    buildDefaultProject();

    resize(1400, 900);
    updateWindowTitle();
}

MainWindow::~MainWindow() {}

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

    QMenu* displayMenu = menuBar()->addMenu("Display(&D)");
    displayMenu->addAction("Zoom In");
    displayMenu->addAction("Zoom Out");
    displayMenu->addAction("Fit to Window");

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
    // 将 Undo/Redo 转发到当前获得焦点的文本控件
    connect(m_aUndo, &QAction::triggered, this, []{
        if (auto* w = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget()))
            w->undo();
    });
    connect(m_aRedo, &QAction::triggered, this, []{
        if (auto* w = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget()))
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
    m_aConnect = tb->addAction(makeLdIcon("connect"), "Connect to PLC  [Ctrl+D]");
    auto* aTransfer = tb->addAction(makeLdIcon("download"), "Transfer Program to PLC");
    m_aRun  = tb->addAction(makeLdIcon("run"),  "Run PLC");
    m_aStop = tb->addAction(makeLdIcon("stop"), "Stop PLC");

    connect(m_aConnect,  &QAction::triggered, this, &MainWindow::connectToPlc);
    connect(aTransfer,   &QAction::triggered, this, [this]{
        if (m_connState != PlcConnState::Connected) {
            statusBar()->showMessage("Not connected to PLC.", 3000); return;
        }
        statusBar()->showMessage("Transferring program to PLC…", 2000);
    });
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
    // 初始状态：未连接时 Run/Stop 禁用
    m_aRun->setEnabled(false);
    m_aStop->setEnabled(false);
    aTransfer->setEnabled(false);
    // 保存 Transfer 引用以便后续 enable/disable
    m_aConnect->setProperty("transferAction",
                            QVariant::fromValue(static_cast<QObject*>(aTransfer)));
    tb->addSeparator();

    // ══════════════════════════════════════════════════════════
    // 第6组：LD / FBD 编辑元件（互斥模式按钮）
    // ══════════════════════════════════════════════════════════
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
        QAction* act = tb->addAction(makeLdIcon(e.iconType), e.tooltip);
        act->setCheckable(true);
        act->setChecked(e.checked);
        modeGroup->addAction(act);
        m_ldModeActions[e.mode] = act;
        connect(act, &QAction::triggered, [this, e](){
            if (m_scene) m_scene->setMode(e.mode);
        });
        // 各子组之间插入分隔符
        if (e.mode == Mode_AddContact_N || e.mode == Mode_AddCoil_R ||
            e.mode == Mode_AddFuncBlock)
            tb->insertSeparator(act);
    }
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

    dock->setWidget(m_projectTree);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

// ============================================================
// 右侧停靠栏：函数库 + 调试器
// ============================================================
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

    const QIcon folderIcon(":/images/FOLDER.png");
    const QStringList categories = {
        "Standard function", "Additional function", "Type conversion",
        "Numerical", "Arithmetic", "Bit-shift", "Bitwise",
        "Selection", "Comparison", "Character string", "Time",
        "User-defined POU"
    };
    for (const QString& cat : categories) {
        auto* item = new QTreeWidgetItem(m_libraryTree, QStringList{cat});
        item->setIcon(0, folderIcon);
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
    libLay->addWidget(m_libraryTree);
    libTabs->addTab(libWidget, "Library");
    libTabs->addTab(new QWidget(), "Debugger");

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

    // 激活不同子窗口时同步 m_scene（任何图形语言都有 scene）
    connect(m_mdiArea, &QMdiArea::subWindowActivated,
            this, [this](QMdiSubWindow* sw) {
        if (!sw) { m_scene = nullptr; return; }
        PouModel* pou = m_subWinPouMap.value(sw, nullptr);
        m_scene = pou ? m_sceneMap.value(pou, nullptr) : nullptr;
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
    root->setExpanded(true);

    // 语言图标映射
    static const QMap<PouLanguage, QString> s_langIcon = {
        { PouLanguage::LD,  ":/images/LD.png"  },
        { PouLanguage::ST,  ":/images/ST.png"  },
        { PouLanguage::IL,  ":/images/IL.png"  },
        { PouLanguage::FBD, ":/images/FBD.png" },
        { PouLanguage::SFC, ":/images/SFC.png" },
    };

    for (PouModel* pou : m_project->pous) {
        auto* item = new QTreeWidgetItem(root, QStringList{pou->name});
        item->setIcon(0, QIcon(s_langIcon.value(pou->language, ":/images/Unknown.png")));
        item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void*>(pou)));
    }

    m_projectTree->expandAll();
}

// ============================================================
// 双击树节点 → 打开对应标签页
// ============================================================
void MainWindow::onTreeDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;
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
            else
                scene->initEmpty();
            m_sceneMap[pou] = scene;
        }

        auto* view = new LadderView();
        view->setScene(scene);

        if (!m_scene) m_scene = scene;

        // 延迟 fitInView，等 view 完成布局后再缩放
        QTimer::singleShot(0, view, [view, scene](){
            QRectF r = scene->itemsBoundingRect().adjusted(-40, -40, 40, 40);
            if (r.isEmpty())
                r = QRectF(0, 0, 800, 600);
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

void MainWindow::saveProject()
{
    if (!m_project) return;
    if (m_project->filePath.isEmpty()) {
        saveProjectAs();
        return;
    }
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

    if (!m_project->saveToFile(path)) {
        QMessageBox::critical(this, "Save Error",
            QString("Failed to save:\n%1").arg(path));
        return;
    }
    updateWindowTitle();
    statusBar()->showMessage(QString("Saved: %1").arg(path), 3000);
}

// ============================================================
// 编译：对当前活跃图形场景生成 C 代码，显示到控制台
// ============================================================
void MainWindow::buildProject()
{
    // 确定要编译的 POU
    PouModel* targetPou = nullptr;
    for (auto it = m_sceneMap.begin(); it != m_sceneMap.end(); ++it) {
        if (it.value() == m_scene) { targetPou = it.key(); break; }
    }

    if (!m_scene || !targetPou) {
        m_consoleEdit->appendPlainText(
            "[ Build ] No graphical POU is active. Open an LD/FBD program first.");
        m_consoleTabs->setCurrentWidget(m_consoleEdit);
        return;
    }

    m_consoleEdit->clear();
    m_consoleEdit->appendPlainText(
        QString("[ Build ] Compiling POU: %1 ...").arg(targetPou->name));

    QString code = CodeGenerator::generate(targetPou->name, m_scene);

    m_consoleEdit->appendPlainText("[ Build ] Done.\n");
    m_consoleEdit->appendPlainText("─────────────────────────────────────────");
    m_consoleEdit->appendPlainText(code);
    m_consoleTabs->setCurrentWidget(m_consoleEdit);

    statusBar()->showMessage(
        QString("Build complete: %1").arg(targetPou->name), 4000);
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

    // Transfer 按钮（存储在 m_aConnect 属性中）
    if (m_aConnect) {
        if (auto* t = qobject_cast<QAction*>(
                m_aConnect->property("transferAction").value<QObject*>()))
            t->setEnabled(connected);
    }

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
