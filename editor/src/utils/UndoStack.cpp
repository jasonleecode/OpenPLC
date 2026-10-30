#include "UndoStack.h"
#include "../editor/scene/LadderScene.h"

// ─────────────────────────────────────────────────────────────
// AddItemCmd
// ─────────────────────────────────────────────────────────────
AddItemCmd::AddItemCmd(QGraphicsScene* scene, QGraphicsItem* item,
                       const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent), m_scene(scene), m_item(item)
{}

AddItemCmd::~AddItemCmd()
{
    if (m_owned) {
        if (auto* scene = qobject_cast<LadderScene*>(m_scene)) scene->forgetEditorItem(m_item);
        delete m_item;
    }
}

void AddItemCmd::redo()
{
    // 若 item 已在 scene（如导线预览已添加），addItem 是无害的幂等操作
    if (auto* scene = qobject_cast<LadderScene*>(m_scene)) scene->attachEditorItem(m_item);
    else m_scene->addItem(m_item);
    m_owned = false;
}

void AddItemCmd::undo()
{
    if (auto* scene = qobject_cast<LadderScene*>(m_scene)) scene->detachEditorItem(m_item);
    else m_scene->removeItem(m_item);
    m_owned = true;
}

// ─────────────────────────────────────────────────────────────
// DeleteItemsCmd
// ─────────────────────────────────────────────────────────────
DeleteItemsCmd::DeleteItemsCmd(QGraphicsScene* scene,
                                const QList<QGraphicsItem*>& items,
                                const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent), m_scene(scene), m_items(items), m_owned(false)
{
    if (auto* editor = qobject_cast<LadderScene*>(scene)) m_items = editor->deletionItems(items);
}

DeleteItemsCmd::~DeleteItemsCmd()
{
    if (m_owned) {
        if (auto* scene = qobject_cast<LadderScene*>(m_scene))
            for (auto* item : m_items) scene->forgetEditorItem(item);
        qDeleteAll(m_items);
    }
}

void DeleteItemsCmd::redo()
{
    for (QGraphicsItem* item : m_items) {
        if (auto* scene = qobject_cast<LadderScene*>(m_scene)) scene->detachEditorItem(item);
        else m_scene->removeItem(item);
    }
    m_owned = true;
}

void DeleteItemsCmd::undo()
{
    for (QGraphicsItem* item : m_items) {
        if (auto* scene = qobject_cast<LadderScene*>(m_scene)) scene->attachEditorItem(item);
        else m_scene->addItem(item);
    }
    m_owned = false;
}

// ─────────────────────────────────────────────────────────────
// MoveItemsCmd
// ─────────────────────────────────────────────────────────────
MoveItemsCmd::MoveItemsCmd(const QList<MoveEntry>& moves,
                            const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent), m_moves(moves)
{}

void MoveItemsCmd::redo()
{
    for (const auto& e : m_moves)
        e.item->setPos(e.after);
}

void MoveItemsCmd::undo()
{
    for (const auto& e : m_moves)
        e.item->setPos(e.before);
}

namespace {
class EditTextCmd : public QUndoCommand {
    QString m_before, m_after;
    std::function<void(const QString&)> m_setter;
public:
    EditTextCmd(QString before, QString after, std::function<void(const QString&)> setter)
        : QUndoCommand("Edit Property"), m_before(before), m_after(after), m_setter(setter) {}
    void redo() override { m_setter(m_after); }
    void undo() override { m_setter(m_before); }
};
}
void editItemText(QGraphicsItem* item, const QString& before, const QString& after,
                  const std::function<void(const QString&)>& setter)
{
    if (before == after) return;
    if (auto* scene = qobject_cast<LadderScene*>(item->scene()))
        scene->undoStack()->push(new EditTextCmd(before, after, setter));
    else setter(after);
}
