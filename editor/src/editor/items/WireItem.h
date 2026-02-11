#pragma once
#include <QGraphicsPathItem>
#include <QPen>

class WireItem : public QGraphicsPathItem {
public:
    // 构造函数：需要起点和终点
    WireItem(const QPointF& startPos, const QPointF& endPos, QGraphicsItem* parent = nullptr);

    // 设置起点和终点 (用于拖拽时动态更新)
    void setStartPos(const QPointF& pos);
    void setEndPos(const QPointF& pos);

    // 获取端点
    QPointF startPos() const { return m_startPos; }
    QPointF endPos() const { return m_endPos; }

    // 作为一个图元，也需要支持类型判断 (为了以后删除或编译)
    enum { Type = UserType + 10 };
    int type() const override { return Type; }

private:
    // 核心算法：计算“正交”路径
    void updatePath();

    QPointF m_startPos;
    QPointF m_endPos;
};