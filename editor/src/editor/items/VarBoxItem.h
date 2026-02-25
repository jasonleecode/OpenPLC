#pragma once
#include "BaseItem.h"
#include <QString>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QInputDialog>

// ─────────────────────────────────────────────────────────────
// VarBoxItem — 梯形图 / FBD 中的变量/常量框
// 对应 PLCopen: inVariable / outVariable / inOutVariable
// ─────────────────────────────────────────────────────────────
class VarBoxItem : public BaseItem
{
    Q_OBJECT
public:
    enum Role { InVar, OutVar, InOutVar };

    explicit VarBoxItem(const QString& expression, Role role,
                        QGraphicsItem* parent = nullptr)
        : BaseItem(parent), m_expr(expression), m_role(role)
    {
        setToolTip("Variable — Double-click to edit");
    }

    // 设置从 PLCopen XML 读取的实际像素尺寸（已乘 kScale）
    void setExplicitSize(qreal w, qreal h) {
        m_w = w; m_h = h;
        prepareGeometryChange();
        update();
    }

    QRectF boundingRect() const override {
        return QRectF(0, 0, m_w, m_h);
    }

    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget*) override
    {
        const bool selected = (option->state & QStyle::State_Selected);

        QColor border = selected ? QColor("#0078D7") : QColor("#2E7D32");
        QColor fill   = selected ? QColor("#E3F2FD") : QColor("#E8F5E9");

        painter->setPen(QPen(border, selected ? 2.0 : 1.5));
        painter->setBrush(fill);
        painter->drawRoundedRect(0, 0, m_w, m_h, 4, 4);

        QFont f("Consolas, Courier New");
        f.setPixelSize(qMax(8, (int)(m_h * 0.38)));
        painter->setFont(f);
        painter->setPen(selected ? QColor("#004A99") : QColor("#1B5E20"));
        painter->drawText(QRectF(3, 0, m_w - 6, m_h),
                          Qt::AlignCenter, m_expr);
    }

    QPointF leftPort()  const override { return mapToScene(0,    m_h / 2); }
    QPointF rightPort() const override { return mapToScene(m_w,  m_h / 2); }

    QString expression() const { return m_expr; }
    Role    role()       const { return m_role; }

    void setExpression(const QString& e) { m_expr = e; update(); }

    void editProperties() override {
        bool ok;
        const QString text = QInputDialog::getText(
            nullptr, "Edit Variable",
            "Expression:", QLineEdit::Normal, m_expr, &ok);
        if (ok && !text.isEmpty()) setExpression(text);
    }

    enum { Type = UserType + 5 };
    int type() const override { return Type; }

protected:
    int portYOffset() const override { return (int)(m_h / 2); }

private:
    QString m_expr;
    Role    m_role;
    qreal   m_w = 100;
    qreal   m_h = 30;
};
