#pragma once
#include "BaseItem.h"

class ContactItem : public BaseItem {
    Q_OBJECT
public:
    enum ContactType { NormalOpen, NormalClosed };

    explicit ContactItem(ContactType type, QGraphicsItem *parent = nullptr);

    // 必须重写的两个纯虚函数
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    // 实现端口位置接口
    QPointF leftPort() const override;
    QPointF rightPort() const override;

    // 设置/获取标签名 (如 "X0.0")
    void setTagName(const QString &name);
    QString tagName() const { return m_tagName; }

private:
    ContactType m_type;
    QString m_tagName;
    
    // 缓存尺寸，优化性能
    const int m_width = 60;  // 3格宽
    const int m_height = 40; // 2格高

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
};