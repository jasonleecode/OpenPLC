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

    static const int W = 100;
    static const int H = 30;

    explicit VarBoxItem(const QString& expression, Role role,
                        QGraphicsItem* parent = nullptr)
        : BaseItem(parent), m_expr(expression), m_role(role)
    {
        setToolTip("Variable — Double-click to edit");
    }

    QRectF boundingRect() const override {
        return QRectF(0, 0, W, H);
    }

    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget*) override
    {
        const bool selected = (option->state & QStyle::State_Selected);

        // 颜色
        QColor border = selected ? QColor("#0078D7") : QColor("#2E7D32");
        QColor fill   = selected ? QColor("#E3F2FD") : QColor("#E8F5E9");

        // 圆角矩形
        painter->setPen(QPen(border, selected ? 2.0 : 1.5));
        painter->setBrush(fill);
        painter->drawRoundedRect(0, 0, W, H, 4, 4);

        // 表达式文本（居中）
        QFont f("Consolas, Courier New");
        f.setPixelSize(11);
        painter->setFont(f);
        painter->setPen(selected ? QColor("#004A99") : QColor("#1B5E20"));
        painter->drawText(QRectF(3, 0, W - 6, H),
                          Qt::AlignCenter, m_expr);

        // 方向箭头（左侧 inOutVar：双箭头；右侧标志）
        if (m_role == InOutVar) {
            painter->setPen(QPen(border, 1.0));
            // 无需额外绘制，边框颜色已区分
        }
    }

    // 对于 inVariable，左端口无效（不接收输入）
    QPointF leftPort()  const override { return mapToScene(0,   H / 2); }
    QPointF rightPort() const override { return mapToScene(W,   H / 2); }

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
    int portYOffset() const override { return H / 2; }

private:
    QString m_expr;
    Role    m_role;
};
