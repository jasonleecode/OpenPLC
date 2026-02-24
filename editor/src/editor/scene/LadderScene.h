#pragma once
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QGraphicsSceneContextMenuEvent>
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

// ──────────────────────────────────────────────
class LadderScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit LadderScene(QObject *parent = nullptr);

    static const int GridSize    = 20;
    static const int LeftRailX   = 60;
    static const int RightRailX  = 1240;
    static const int RungHeight  = 100;
    static const int RailTopY    = -40;
    static const int RailBottomY = 2000;

    void setMode(EditorMode mode);
    EditorMode currentMode() const { return m_mode; }

signals:
    void modeChanged(EditorMode mode);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event)  override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;

private:
    // 返回离 pos 最近的元件端口场景坐标（在 radius 以内）；无则返回 pos
    QPointF snapToNearestPort(const QPointF& pos, qreal radius) const;

    EditorMode m_mode;

    QColor m_backgroundColor;
    QColor m_gridColor;
    QColor m_gridColorFine;

    WireItem* m_tempWire = nullptr;

    // 端口吸附指示器（导线拉线时显示绿色圆圈）
    QPointF m_portSnapPos;
    bool    m_showPortSnap = false;

    int m_contactCount = 0;
    int m_coilCount    = 0;
    int m_fbCount      = 0;
};
