#pragma once
#include <QGraphicsPathItem>
#include <QPen>

class WireItem : public QGraphicsPathItem {
public:
    WireItem(const QPointF& startPos, const QPointF& endPos,
             QGraphicsItem* parent = nullptr);

    void setStartPos(const QPointF& pos);
    void setEndPos(const QPointF& pos);

    QPointF startPos() const { return m_startPos; }
    QPointF endPos()   const { return m_endPos;   }

    // 选中态蓝色高亮
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    // 加宽点击区域（10 px），方便鼠标拾取
    QPainterPath shape() const override;

    enum { Type = UserType + 10 };
    int type() const override { return Type; }

private:
    void updatePath();

    QPointF m_startPos;
    QPointF m_endPos;
};
