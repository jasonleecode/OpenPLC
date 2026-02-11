#pragma once
#include "BaseItem.h"

class CoilItem : public BaseItem {
    Q_OBJECT
public:
    explicit CoilItem(QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    QPointF leftPort() const override;
    QPointF rightPort() const override;

    void setTagName(const QString &name);
    QString tagName() const { return m_tagName; }

private:
    QString m_tagName;
    const int m_width = 60;
    const int m_height = 40;

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    
};