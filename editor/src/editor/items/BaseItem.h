// 所有图元的基类 (处理吸附)

#pragma once
#include <QGraphicsObject>
#include <QPen>
#include <QBrush>
#include <QGraphicsSceneMouseEvent>

class BaseItem : public QGraphicsObject {
    Q_OBJECT
public:
    explicit BaseItem(QGraphicsItem *parent = nullptr) : QGraphicsObject(parent) {
        setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges | ItemIsFocusable);
    }

    virtual QPointF leftPort()  const = 0;
    virtual QPointF rightPort() const = 0;

    // 右键菜单 / 双击编辑：弹出属性对话框（子类重写）
    virtual void editProperties() {}

protected:
    static constexpr int GridSize  = 20;   // 必须与 LadderScene::GridSize 一致
    static constexpr int RungH     = 100;  // 必须与 LadderScene::RungHeight 一致
    static constexpr int RungBaseY = 50;   // 第一个梯级中心 Y（= RungH / 2）

    // 当前元件的端口相对于 pos() 的 Y 偏移（用于梯级吸附）
    // 默认 20（ContactItem / CoilItem 的 H/2 = 40/2 = 20）
    virtual int portYOffset() const { return 20; }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override {
        Q_UNUSED(event);
        editProperties();
    }

    // 位置变化时：X 吸附到网格，Y 吸附到最近梯级中心
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override {
        if (change == ItemPositionChange && scene()) {
            QPointF p = value.toPointF();

            // X: 20px 网格
            qreal x = qRound(p.x() / GridSize) * (qreal)GridSize;

            // Y: 端口对齐到最近梯级中心
            qreal portY  = p.y() + portYOffset();
            int   rung   = qRound((portY - RungBaseY) / (qreal)RungH);
            rung = qMax(0, rung);
            qreal y = RungBaseY + rung * RungH - portYOffset();

            return QPointF(x, y);
        }
        return QGraphicsObject::itemChange(change, value);
    }
};
