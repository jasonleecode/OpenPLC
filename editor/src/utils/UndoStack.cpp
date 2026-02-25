#include "UndoStack.h"

// ─────────────────────────────────────────────────────────────
// AddItemCmd
// ─────────────────────────────────────────────────────────────
AddItemCmd::AddItemCmd(QGraphicsScene* scene, QGraphicsItem* item,
                       const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent), m_scene(scene), m_item(item)
{}

AddItemCmd::~AddItemCmd()
{
    if (m_owned) delete m_item;
}

void AddItemCmd::redo()
{
    // 若 item 已在 scene（如导线预览已添加），addItem 是无害的幂等操作
    m_scene->addItem(m_item);
    m_owned = false;
}

void AddItemCmd::undo()
{
    m_scene->removeItem(m_item);
    m_owned = true;
}

// ─────────────────────────────────────────────────────────────
// DeleteItemsCmd
// ─────────────────────────────────────────────────────────────
DeleteItemsCmd::DeleteItemsCmd(QGraphicsScene* scene,
                                const QList<QGraphicsItem*>& items,
                                const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent), m_scene(scene), m_items(items), m_owned(false)
{}

DeleteItemsCmd::~DeleteItemsCmd()
{
    if (m_owned) qDeleteAll(m_items);
}

void DeleteItemsCmd::redo()
{
    for (QGraphicsItem* item : m_items)
        m_scene->removeItem(item);
    m_owned = true;
}

void DeleteItemsCmd::undo()
{
    for (QGraphicsItem* item : m_items)
        m_scene->addItem(item);
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
