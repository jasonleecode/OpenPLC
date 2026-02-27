#pragma once
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QUndoStack>
#include <QHash>
#include <QMap>
#include "../items/WireItem.h"

// ──────────────────────────────────────────────
// 编辑模式枚举（IEC 61131-3 常用元件）
// ──────────────────────────────────────────────
enum EditorMode {
    Mode_Select,

    Mode_AddContact_NO,  // 常开触点  -| |-
    Mode_AddContact_NC,  // 常闭触点  -|/|-
    Mode_AddContact_P,   // 上升沿触点 -|P|-
    Mode_AddContact_N,   // 下降沿触点 -|N|-

    Mode_AddCoil,        // 输出线圈  -( )-
    Mode_AddCoil_S,      // 置位线圈  -(S)-
    Mode_AddCoil_R,      // 复位线圈  -(R)-

    Mode_AddFuncBlock,   // 功能块（TON/CTU 等矩形块）

    Mode_AddWire,
};

class BaseItem;
class FunctionBlockItem;

// ──────────────────────────────────────────────────────────────────────────
// LadderScene — 基础图形编辑场景
//
//   • 提供完整编辑能力：模式管理、元件放置、导线绘制、端口吸附
//   • 内置撤销/重做（QUndoStack）
//   • item 追踪（m_items，供序列化使用）
//   • 默认 drawBackground 绘制 LD 电源母线 + 梯级背景（可被子类覆盖）
//
// 子类只需 override drawBackground 即可得到不同的背景风格。
// ──────────────────────────────────────────────────────────────────────────
class LadderScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit LadderScene(QObject *parent = nullptr);

    // ── LD 布局常量 ─────────────────────────────────────────────
    static const int GridSize    = 20;
    static const int LeftRailX   = 60;
    static const int RightRailX  = 1240;
    static const int RungHeight  = 100;
    static const int RailTopY    = -40;
    static const int RailBottomY = 2000;

    void       setMode(EditorMode mode);
    EditorMode currentMode() const { return m_mode; }

    QUndoStack* undoStack() const { return m_undoStack; }

signals:
    void modeChanged(EditorMode mode);

protected:
    // 虚函数：子类可覆盖（PlcOpenViewer 覆盖为简单点阵）
    void drawBackground  (QPainter *painter, const QRectF &rect) override;
    void drawForeground  (QPainter *painter, const QRectF &rect) override;
    void mousePressEvent  (QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent   (QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void keyPressEvent    (QKeyEvent *event) override;
    void contextMenuEvent (QGraphicsSceneContextMenuEvent *event) override;

    // 返回离 pos 最近的元件端口场景坐标（在 radius 以内）；无则返回 pos
    QPointF snapToNearestPort(const QPointF& pos, qreal radius) const;

    // ── 共享编辑状态（子类直接访问）────────────────────────────
    EditorMode m_mode         = Mode_Select;
    WireItem*  m_tempWire     = nullptr;
    QPointF    m_portSnapPos;
    bool       m_showPortSnap = false;
    int        m_contactCount = 0;
    int        m_coilCount    = 0;
    int        m_fbCount      = 0;

    // item 追踪：localId → scene item（序列化 / undo 共用）
    QMap<int, QGraphicsItem*>      m_items;
    int                            m_nextLocalId = 10000;

    // 撤销/重做
    QUndoStack*                    m_undoStack    = nullptr;
    QHash<QGraphicsItem*, QPointF> m_dragStartPos;

private:
    // LD 背景绘制用色（只在默认 drawBackground 中使用）
    QColor m_backgroundColor;
    QColor m_gridColor;
    QColor m_gridColorFine;
};
