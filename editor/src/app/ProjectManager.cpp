#include "ProjectManager.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>

#include "../core/models/ProjectModel.h"
#include "../core/models/PouModel.h"
#include "../editor/scene/PlcOpenViewer.h"

ProjectManager::ProjectManager(QWidget* dialogParent, QObject* parent)
    : QObject(parent), m_parent(dialogParent)
{}

void ProjectManager::setSceneMap(QMap<PouModel*, PlcOpenViewer*>* map)
{
    m_sceneMap = map;
}

// ============================================================
// 保存前同步（static，供 buildProject() 也可调用）
// ============================================================
void ProjectManager::syncScenesBeforeSave(const QMap<PouModel*, PlcOpenViewer*>& map)
{
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        PouModel*      pou   = it.key();
        PlcOpenViewer* scene = it.value();
        if (!scene) continue;
        const QString xml = scene->toXmlString();
        if (!xml.isEmpty())
            pou->graphicalXml = xml;
    }
}

// ============================================================
// 新建项目
// ============================================================
void ProjectManager::newProject()
{
    if (m_project && m_project->isDirty()) {
        const int ret = QMessageBox::question(m_parent, "New Project",
            "Current project has unsaved changes. Discard them?",
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    const QString name = QInputDialog::getText(
        m_parent, "New Project", "Project name:", QLineEdit::Normal, "Untitled");
    if (name.trimmed().isEmpty()) return;

    auto* proj = new ProjectModel(nullptr);
    proj->projectName = name.trimmed();
    PouModel* pou = proj->addPou("main", PouType::Program, PouLanguage::LD);
    proj->clearDirty();

    m_project = proj;
    emit projectCreated(proj);
    emit firstPouReady(pou);
    emit titleUpdateNeeded();
}

// ============================================================
// 打开项目
// ============================================================
void ProjectManager::openProject()
{
    const QString path = QFileDialog::getOpenFileName(
        m_parent, "Open Project", QString(),
        "TiZi Project (*.tizi);;XML Files (*.xml);;All Files (*)");
    if (path.isEmpty()) return;

    auto* proj = new ProjectModel(nullptr);
    if (!proj->loadFromFile(path)) {
        QMessageBox::critical(m_parent, "Open Error",
            QString("Failed to open:\n%1").arg(path));
        delete proj;
        return;
    }

    m_project = proj;
    emit projectCreated(proj);
    if (!proj->pous.isEmpty())
        emit firstPouReady(proj->pous.first());
    emit titleUpdateNeeded();
    emit statusMessage(QString("Opened: %1").arg(path), 3000);
}

// ============================================================
// 保存（覆盖）
// ============================================================
void ProjectManager::saveProject()
{
    if (!m_project) return;
    if (m_project->filePath.isEmpty()) {
        saveProjectAs();
        return;
    }
    doSaveTo(m_project->filePath);
}

// ============================================================
// 另存为
// ============================================================
void ProjectManager::saveProjectAs()
{
    if (!m_project) return;
    const QString path = QFileDialog::getSaveFileName(
        m_parent, "Save Project As", m_project->projectName + ".tizi",
        "TiZi Project (*.tizi);;XML Files (*.xml);;All Files (*)");
    if (path.isEmpty()) return;
    doSaveTo(path);
}

void ProjectManager::doSaveTo(const QString& path)
{
    if (m_sceneMap)
        syncScenesBeforeSave(*m_sceneMap);
    if (!m_project->saveToFile(path)) {
        QMessageBox::critical(m_parent, "Save Error",
            QString("Failed to save:\n%1").arg(path));
        return;
    }
    emit titleUpdateNeeded();
    emit statusMessage("Saved.", 3000);
}

// ============================================================
// 启动时的内置示例项目
// ============================================================
void ProjectManager::buildDefaultProject()
{
    auto* proj = new ProjectModel(nullptr);
    proj->projectName = "First Steps";

    // CounterLD
    PouModel* ld = proj->addPou("CounterLD", PouType::FunctionBlock, PouLanguage::LD);
    ld->description = "Counter using Ladder Diagram";
    ld->variables.append({"Reset", "Input",  "BOOL", "", ""});
    ld->variables.append({"Out",   "Output", "INT",  "", ""});

    // CounterST
    PouModel* st = proj->addPou("CounterST", PouType::FunctionBlock, PouLanguage::ST);
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
    PouModel* il = proj->addPou("CounterIL", PouType::FunctionBlock, PouLanguage::IL);
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

    proj->clearDirty();

    m_project = proj;
    emit projectCreated(proj);
    if (!proj->pous.isEmpty())
        emit firstPouReady(proj->pous.first());
}
