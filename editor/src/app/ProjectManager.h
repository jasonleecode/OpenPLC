#pragma once
#include <QObject>
#include <QMap>

class ProjectModel;
class PouModel;
class PlcOpenViewer;
class QWidget;

// ──────────────────────────────────────────────────────────────────
// ProjectManager — 项目生命周期管理（新建 / 打开 / 保存）
//
// 通过 Qt 信号通知 MainWindow 做 UI 动作，不依赖 MainWindow 类型，
// 保持单向依赖（MainWindow → ProjectManager）。
//
// 使用方式：
//   1. 创建：new ProjectManager(dialogParent, this)
//   2. 连接四个信号
//   3. 调用 setSceneMap(&m_sceneMap)
//   4. 调用 buildDefaultProject() 创建启动项目
// ──────────────────────────────────────────────────────────────────
class ProjectManager : public QObject {
    Q_OBJECT
public:
    explicit ProjectManager(QWidget* dialogParent, QObject* parent = nullptr);

    // ── 必须在 buildDefaultProject() 之前调用 ────────────────────
    void setSceneMap(QMap<PouModel*, PlcOpenViewer*>* map);

    // ── 项目操作 ──────────────────────────────────────────────────
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();

    // ── 启动时调用一次，创建内置示例项目 ─────────────────────────
    void buildDefaultProject();

    // ── 供 buildProject() 在自动保存前同步场景 ───────────────────
    static void syncScenesBeforeSave(const QMap<PouModel*, PlcOpenViewer*>& map);

signals:
    // 新/打开项目完成：MainWindow 应接管 project 的所有权（setParent / delete 旧的）
    // 接管后应 closeAllPouTabs() + m_sceneMap.clear() + rebuildProjectTree()
    void projectCreated(ProjectModel* project);

    // 新/打开后建议打开的第一个 POU
    void firstPouReady(PouModel* pou);

    // 标题需要刷新（项目名变化或脏标记变化后）
    void titleUpdateNeeded();

    // 状态栏临时消息
    void statusMessage(const QString& msg, int timeoutMs);

private:
    void doSaveTo(const QString& path);

    ProjectModel*                    m_project = nullptr;
    QMap<PouModel*, PlcOpenViewer*>* m_sceneMap = nullptr;
    QWidget*                         m_parent;
};
