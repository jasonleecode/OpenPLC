// 连线（正交路由 + 选中高亮）

#include "WireItem.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>

WireItem::WireItem(const QPointF& startPos, const QPointF& endPos,
                   QGraphicsItem* parent)
    : QGraphicsPathItem(parent), m_startPos(startPos), m_endPos(endPos)
{
    setFlag(ItemIsSelectable, true);
    setPen(QPen(QColor("#1A2E4A"), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    updatePath();
}

void WireItem::setStartPos(const QPointF& pos) {
    m_startPos = pos;
    updatePath();
}

void WireItem::setEndPos(const QPointF& pos) {
    m_endPos = pos;
    updatePath();
}

void WireItem::paint(QPainter *painter,
                     const QStyleOptionGraphicsItem *option,
                     QWidget*)
{
    const bool selected = option->state & QStyle::State_Selected;
    painter->setPen(QPen(
        selected ? QColor("#0078D7") : QColor("#1A2E4A"),
        selected ? 2.5 : 2.0,
        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());
}

void WireItem::updatePath()
{
    QPainterPath path;
    path.moveTo(m_startPos);

    // 正交路由：水平 → 垂直 → 水平（L 形或 Z 形）
    double midX = (m_startPos.x() + m_endPos.x()) / 2.0;
    path.lineTo(midX, m_startPos.y());
    path.lineTo(midX, m_endPos.y());
    path.lineTo(m_endPos.x(), m_endPos.y());

    setPath(path);
}
