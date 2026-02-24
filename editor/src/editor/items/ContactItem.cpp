#include "ContactItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QInputDialog>
#include <QGraphicsSceneMouseEvent>

ContactItem::ContactItem(ContactType type, QGraphicsItem *parent)
    : BaseItem(parent), m_type(type), m_tagName("??")
{
    setToolTip("Contact — Double-click to edit variable name");
}

QRectF ContactItem::boundingRect() const {
    // 上方留出 22px 放标签，整体宽 W 高 H+22
    return QRectF(0, -22, W, H + 22);
}

void ContactItem::paint(QPainter *painter,
                        const QStyleOptionGraphicsItem *option,
                        QWidget*)
{
    const bool selected = (option->state & QStyle::State_Selected);
    const QColor lineColor = selected ? QColor("#0078D7") : QColor("#1A1A1A");

    // ── 1. 引线（左/右水平线） ────────────────────────────────
    QPen wirePen(lineColor, 2, Qt::SolidLine, Qt::FlatCap);
    painter->setPen(wirePen);
    painter->drawLine( 0, H/2, 15, H/2);   // 左引线
    painter->drawLine(45, H/2, W,  H/2);   // 右引线

    // ── 2. 触点竖条（左/右竖线） ─────────────────────────────
    QPen barPen(lineColor, 2.5, Qt::SolidLine, Qt::FlatCap);
    painter->setPen(barPen);
    painter->drawLine(15,  4, 15, H - 4);  // 左竖条
    painter->drawLine(45,  4, 45, H - 4);  // 右竖条

    // ── 3. 类型标记 ──────────────────────────────────────────
    painter->setPen(QPen(lineColor, 1.5));
    switch (m_type) {
    case NormalClosed:
        // 斜杠 /
        painter->drawLine(16, H - 5, 44, 5);
        break;

    case PositiveTransition:
    case NegativeTransition: {
        // 中间小方框 + P / N
        QRectF box(18, 6, 24, H - 12);
        painter->setPen(QPen(lineColor, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(box);
        QFont f; f.setPixelSize(11); f.setBold(true);
        painter->setFont(f);
        painter->setPen(lineColor);
        painter->drawText(box, Qt::AlignCenter,
                          m_type == PositiveTransition ? "P" : "N");
        break;
    }
    default:
        break;
    }

    // ── 4. 变量标签（元件上方居中） ──────────────────────────
    QFont labelFont;
    labelFont.setFamily("Consolas, Courier New");
    labelFont.setPixelSize(11);
    painter->setFont(labelFont);
    painter->setPen(selected ? QColor("#0057A8") : QColor("#333333"));
    painter->drawText(QRectF(0, -21, W, 18), Qt::AlignCenter, m_tagName);
}

QPointF ContactItem::leftPort()  const { return mapToScene(0,  H/2); }
QPointF ContactItem::rightPort() const { return mapToScene(W,  H/2); }

void ContactItem::setTagName(const QString &name) {
    m_tagName = name;
    update();
}

void ContactItem::editProperties() {
    bool ok;
    const QString text = QInputDialog::getText(
        nullptr, "Edit Contact",
        "Variable name (e.g. Reset):",
        QLineEdit::Normal, m_tagName, &ok);
    if (ok && !text.isEmpty())
        setTagName(text);
}
