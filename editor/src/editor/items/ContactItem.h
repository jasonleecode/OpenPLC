#pragma once
#include "BaseItem.h"

class ContactItem : public BaseItem {
    Q_OBJECT
public:
    enum ContactType {
        NormalOpen,          // 常开  -| |-
        NormalClosed,        // 常闭  -|/|-
        PositiveTransition,  // 上升沿 -|P|-
        NegativeTransition,  // 下降沿 -|N|-
    };

    explicit ContactItem(ContactType type, QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    QPointF leftPort()  const override;
    QPointF rightPort() const override;

    void        setTagName(const QString &name);
    QString     tagName()     const { return m_tagName; }
    ContactType contactType() const { return m_type;    }

    void editProperties() override;

    // 设置从 PLCopen XML 读取的实际像素尺寸（已乘 kScale）
    void setExplicitSize(qreal w, qreal h);

    enum { Type = UserType + 1 };
    int type() const override { return Type; }

    static const int W = 60;
    static const int H = 40;

private:
    ContactType m_type;
    QString     m_tagName;
    qreal       m_w = W;
    qreal       m_h = H;
};
