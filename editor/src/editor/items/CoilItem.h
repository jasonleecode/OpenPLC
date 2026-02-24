#pragma once
#include "BaseItem.h"

class CoilItem : public BaseItem {
    Q_OBJECT
public:
    enum CoilType {
        Output,    // 输出线圈  -( )-
        SetCoil,   // 置位线圈  -(S)-  保持 ON
        ResetCoil, // 复位线圈  -(R)-  保持 OFF
        Negated,   // 取反线圈  -(/)-  NOT output
    };

    explicit CoilItem(CoilType type = Output, QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    QPointF leftPort()  const override;
    QPointF rightPort() const override;

    void     setTagName(const QString &name);
    QString  tagName()  const { return m_tagName; }
    CoilType coilType() const { return m_type;    }

    void editProperties() override;

    enum { Type = UserType + 2 };
    int type() const override { return Type; }

    static const int W = 60;
    static const int H = 40;

private:
    CoilType m_type;
    QString  m_tagName;
};
