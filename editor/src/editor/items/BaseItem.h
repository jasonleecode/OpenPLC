// 所有图元的基类 (处理吸附)

#pragma once
#include <QGraphicsObject>
#include <QPen>
#include <QBrush>
#include <QGraphicsSceneMouseEvent>

// 继承自 QGraphicsObject 是为了以后支持 Qt 的属性系统和信号槽
class BaseItem : public QGraphicsObject {
    Q_OBJECT
public:
    explicit BaseItem(QGraphicsItem *parent = nullptr) : QGraphicsObject(parent) {
        // 开启关键标志位：
        // ItemIsSelectable: 允许鼠标点击选中
        // ItemIsMovable: 允许鼠标拖拽移动
        // ItemSendsGeometryChanges: 移动时触发 itemChange，用于网格吸附
        setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges | ItemIsFocusable);
    }

    // 纯虚函数：每个子类必须告诉我们它的左/右连接点在哪里（为了以后画线）
    virtual QPointF leftPort() const = 0;
    virtual QPointF rightPort() const = 0;

protected:
    // 网格大小，必须和 Scene 保持一致
    const int GridSize = 20;

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override {
        Q_UNUSED(event); // 默认什么都不做
    }

    // 核心：处理网格吸附
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override {
        if (change == ItemPositionChange && scene()) {
            QPointF newPos = value.toPointF();
            
            // 简单的数学四舍五入算法
            qreal x = round(newPos.x() / GridSize) * GridSize;
            qreal y = round(newPos.y() / GridSize) * GridSize;
            
            return QPointF(x, y);
        }
        return QGraphicsObject::itemChange(change, value);
    }
};