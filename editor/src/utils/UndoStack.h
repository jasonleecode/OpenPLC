#pragma once
#include <QUndoCommand>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QPointF>
#include <QList>

// ── 放置单个元件 ──────────────────────────────────────────────
//   redo : addItem  (命令放弃所有权 → scene 持有)
//   undo : removeItem (命令重新持有)
//   析构 : 若仍持有则 delete
class AddItemCmd : public QUndoCommand
{
public:
    AddItemCmd(QGraphicsScene* scene, QGraphicsItem* item,
               const QString& text, QUndoCommand* parent = nullptr);
    ~AddItemCmd() override;
    void redo() override;
    void undo() override;

private:
    QGraphicsScene* m_scene;
    QGraphicsItem*  m_item;
    bool            m_owned = true;   // true = 命令持有（item 不在 scene 中）
};

// ── 删除多个元件 ──────────────────────────────────────────────
//   redo : removeItem × N (命令获得所有权)
//   undo : addItem × N    (命令放弃所有权 → scene 持有)
//   析构 : 若持有则 qDeleteAll
class DeleteItemsCmd : public QUndoCommand
{
public:
    DeleteItemsCmd(QGraphicsScene* scene, const QList<QGraphicsItem*>& items,
                   const QString& text, QUndoCommand* parent = nullptr);
    ~DeleteItemsCmd() override;
    void redo() override;
    void undo() override;

private:
    QGraphicsScene*       m_scene;
    QList<QGraphicsItem*> m_items;
    bool                  m_owned = false;  // 初始时 items 仍在 scene 中
};

// ── 移动元件（拖拽结束后记录）────────────────────────────────
//   redo : 应用移动后坐标
//   undo : 恢复移动前坐标
struct MoveEntry { QGraphicsItem* item; QPointF before; QPointF after; };

class MoveItemsCmd : public QUndoCommand
{
public:
    MoveItemsCmd(const QList<MoveEntry>& moves,
                 const QString& text, QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    QList<MoveEntry> m_moves;
};
