// 连线 (正交路由算法在这里)

#include "WireItem.h"
#include <QPen>

WireItem::WireItem(const QPointF& startPos, const QPointF& endPos, QGraphicsItem* parent)
    : QGraphicsPathItem(parent), m_startPos(startPos), m_endPos(endPos)
{
    // 设置线条样式：黑色，2像素宽
    setPen(QPen(Qt::black, 2));
    
    // 初始化路径
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

void WireItem::updatePath() {
    QPainterPath path;
    path.moveTo(m_startPos);

    // === 简单的正交路由算法 ===
    // 策略：先水平走一半，再垂直走，最后水平走
    // 这是梯形图最常见的走线方式
    
    double midX = (m_startPos.x() + m_endPos.x()) / 2;
    
    // 1. 第一段：水平走到中间 X
    path.lineTo(midX, m_startPos.y());
    
    // 2. 第二段：垂直走到终点 Y
    path.lineTo(midX, m_endPos.y());
    
    // 3. 第三段：水平走到终点 X
    path.lineTo(m_endPos.x(), m_endPos.y());

    setPath(path);
}