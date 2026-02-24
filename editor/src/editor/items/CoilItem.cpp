#include "CoilItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QInputDialog>
#include <QGraphicsSceneMouseEvent>

CoilItem::CoilItem(CoilType type, QGraphicsItem *parent)
    : BaseItem(parent), m_type(type), m_tagName("Y0")
{
    setToolTip("Coil — Double-click to edit variable name");
}

QRectF CoilItem::boundingRect() const {
    return QRectF(0, -22, W, H + 22);
}

void CoilItem::paint(QPainter *painter,
                     const QStyleOptionGraphicsItem *option,
                     QWidget*)
{
    const bool selected = (option->state & QStyle::State_Selected);
    const QColor lineColor = selected ? QColor("#0078D7") : QColor("#1A1A1A");

    QPen pen(lineColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);

    // ── 1. 左右引线 ───────────────────────────────────────────
    painter->drawLine(0,  H/2, 14, H/2);  // 左引线到弧起点
    painter->drawLine(46, H/2, W,  H/2);  // 右引线到弧终点

    // ── 2. 线圈弧 ( ) ─────────────────────────────────────────
    // 左弧 (
    painter->drawArc(10, H/2 - 14, 12, 28, 90 * 16, 180 * 16);
    // 右弧 )
    painter->drawArc(38, H/2 - 14, 12, 28, 90 * 16, -180 * 16);

    // ── 3. 类型标记 ──────────────────────────────────────────
    switch (m_type) {
    case SetCoil:
    case ResetCoil: {
        QFont f; f.setPixelSize(12); f.setBold(true);
        painter->setFont(f);
        painter->setPen(lineColor);
        painter->drawText(
            QRectF(13, H/2 - 10, 34, 20), Qt::AlignCenter,
            m_type == SetCoil ? "S" : "R");
        break;
    }
    case Negated: {
        QPen slashPen(lineColor, 1.5);
        painter->setPen(slashPen);
        painter->drawLine(22, H/2 + 8, 38, H/2 - 8);  // 斜杠
        break;
    }
    default:
        break;
    }

    // ── 4. 变量标签（上方居中） ──────────────────────────────
    QFont labelFont;
    labelFont.setFamily("Consolas, Courier New");
    labelFont.setPixelSize(11);
    painter->setFont(labelFont);
    painter->setPen(selected ? QColor("#0057A8") : QColor("#333333"));
    painter->drawText(QRectF(0, -21, W, 18), Qt::AlignCenter, m_tagName);
}

QPointF CoilItem::leftPort()  const { return mapToScene(0, H/2); }
QPointF CoilItem::rightPort() const { return mapToScene(W, H/2); }

void CoilItem::setTagName(const QString &name) {
    m_tagName = name;
    update();
}

void CoilItem::editProperties() {
    bool ok;
    const QString text = QInputDialog::getText(
        nullptr, "Edit Coil",
        "Variable name (e.g. Motor_On):",
        QLineEdit::Normal, m_tagName, &ok);
    if (ok && !text.isEmpty())
        setTagName(text);
}
