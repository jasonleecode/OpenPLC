#pragma once
#include <QMainWindow>
#include <QMap>
#include <QIcon>

#include "../core/models/ProjectModel.h"
#include "../editor/scene/LadderScene.h"     // EditorMode 枚举

class QMdiArea;
class QMdiSubWindow;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QPlainTextEdit;
class QLabel;
class PlcOpenViewer;
class LadderView;

// ─────────────────────────────────────────────────────────────
// PLC 连接状态
// ─────────────────────────────────────────────────────────────
enum class PlcConnState {
    Disconnected,   // 未连接（灰色）
    Connecting,     // 正在连接（黄色）
    Connected,      // 已连接（绿色）
};

// PLC 运行状态（仅 Connected 时有意义）
enum class PlcRunState {
    Unknown,        // 未知（灰色）
    Stopped,        // 已停止（橙色）
    Running,        // 运行中（绿色）
    Paused,         // 暂停（黄色）
    Error,          // 错误（红色）
};

// ─────────────────────────────────────────────────────────────
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // 供外部（网络线程等）更新 PLC 状态
    void setPlcConnState(PlcConnState state);
    void setPlcRunState (PlcRunState  state);

private:
    // ---- 初始化各区域 ----
    void setupMenuBar();
    void setupToolBar();
    void setupProjectPanel();
    void setupLibraryPanel();
    void setupConsolePanel();
    void setupCentralArea();
    void setupStatusBar();   // 状态栏 PLC 状态指示器

    // ---- 项目操作 ----
    void buildDefaultProject();
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void buildProject();     // 编译：生成 C 代码
    void connectToPlc();     // 连接/断开 PLC

    // ---- 项目树 ----
    void rebuildProjectTree();
    void onTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onTreeContextMenu(const QPoint& pos);

    // ---- 子窗口管理 ----
    void openPouTab(PouModel* pou);
    void closeAllPouTabs();

    // ---- POU 编辑器工厂 ----
    QWidget* createPouEditorWidget(PouModel* pou);
    QWidget* createVarDeclWidget(PouModel* pou);

    // ---- 窗口标题 ----
    void updateWindowTitle();

    // ---- 视图缩放 ----
    LadderView* activeView() const;   // 当前活跃的图形视图（可能为 nullptr）
    void zoomIn();
    void zoomOut();
    void zoomFit();

    // ---- 工具栏辅助 ----
    static QIcon makeLdIcon(const QString& type, int size = 24);
    void onLdModeChanged(EditorMode mode);

    // ---- 状态栏辅助 ----
    static QString ledStyle(const QString& color);  // 生成圆形 LED 样式

    // ---- 事件过滤（状态栏点击）────────────────────────────────
    bool eventFilter(QObject* obj, QEvent* ev) override;

    // ---- 成员变量 ----
    ProjectModel*   m_project     = nullptr;
    PlcOpenViewer*  m_scene       = nullptr;   // 当前活跃图形场景
    QMdiArea*       m_mdiArea     = nullptr;
    QTreeWidget*    m_projectTree = nullptr;
    QTreeWidget*    m_libraryTree = nullptr;
    QTabWidget*     m_consoleTabs = nullptr;
    QPlainTextEdit* m_consoleEdit = nullptr;

    // ---- PLC 状态 ----
    PlcConnState    m_connState = PlcConnState::Disconnected;
    PlcRunState     m_runState  = PlcRunState::Unknown;
    QString         m_plcUri;               // e.g. "PYRO://localhost:61131"

    // 状态栏永久控件
    QLabel* m_connLed      = nullptr;       // 连接状态 LED
    QLabel* m_connLabel    = nullptr;       // 连接状态文字
    QLabel* m_stateLed     = nullptr;       // 运行状态 LED
    QLabel* m_stateLabel   = nullptr;       // 运行状态文字
    QLabel* m_uriLabel     = nullptr;       // PLC 地址

    // 每个 MDI 子窗口对应的 PouModel
    QMap<QMdiSubWindow*, PouModel*> m_subWinPouMap;
    // 每个 PouModel 对应的图形场景（LD / FBD / SFC，关闭后保留复用）
    QMap<PouModel*, PlcOpenViewer*> m_sceneMap;
    // LD 工具栏模式按钮映射（Escape / signal 时同步状态）
    QMap<EditorMode, QAction*> m_ldModeActions;

    // 工具栏动作（需随状态动态 enable/disable）
    QAction* m_aUndo    = nullptr;
    QAction* m_aRedo    = nullptr;
    QAction* m_aConnect = nullptr;   // 连接/断开（图标随状态切换）
    QAction* m_aRun     = nullptr;
    QAction* m_aStop    = nullptr;
};
