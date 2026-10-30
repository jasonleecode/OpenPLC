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

signals:
    void contentChanged();

protected:
    static constexpr int GridSize  = 20;   // 必须与 LadderScene::GridSize 一致
    static constexpr int MoveGridSize = 5; // 拖动时使用细网格，便于与连线对齐
    static constexpr int RungH     = 100;  // 必须与 LadderScene::RungHeight 一致
    static constexpr int RungBaseY = 50;   // 第一个梯级中心 Y（= RungH / 2）

    // 当前元件的端口相对于 pos() 的 Y 偏移（用于梯级吸附）
    // 默认 20（ContactItem / CoilItem 的 H/2 = 40/2 = 20）
    virtual int portYOffset() const { return 20; }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override {
        Q_UNUSED(event);
        editProperties();
    }

    // 位置变化时使用细网格吸附；放置元件/画线仍由场景使用 GridSize。
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override {
        if (change == ItemPositionChange && scene()) {
            QPointF p = value.toPointF();
            qreal x = qRound(p.x() / MoveGridSize) * (qreal)MoveGridSize;
            qreal y = qRound(p.y() / MoveGridSize) * (qreal)MoveGridSize;
            return QPointF(x, y);
        }
        return QGraphicsObject::itemChange(change, value);
    }
};
