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

void ContactItem::setExplicitSize(qreal w, qreal h) {
    m_w = w; m_h = h;
    prepareGeometryChange();
    update();
}

QRectF ContactItem::boundingRect() const {
    // 上方留出空间放标签
    qreal labelH = qMin(m_h * 0.55, 22.0);
    return QRectF(0, -labelH, m_w, m_h + labelH);
}

void ContactItem::paint(QPainter *painter,
                        const QStyleOptionGraphicsItem *option,
                        QWidget*)
{
    const bool selected = (option->state & QStyle::State_Selected);
    const QColor lineColor = selected ? QColor("#0078D7") : QColor("#1A1A1A");

    // 使用 m_w/m_h 按比例绘制（保持触点符号的相对比例）
    const qreal w  = m_w;
    const qreal h  = m_h;
    const qreal my = h / 2.0;
    // 左/右竖条位置：占总宽的 25% 和 75%
    const qreal lx = w * 0.25;
    const qreal rx = w * 0.75;

    // ── 1. 引线（左/右水平线） ────────────────────────────────
    QPen wirePen(lineColor, qMax(1.0, h * 0.05), Qt::SolidLine, Qt::FlatCap);
    painter->setPen(wirePen);
    painter->drawLine(QPointF(0,  my), QPointF(lx, my));
    painter->drawLine(QPointF(rx, my), QPointF(w,  my));

    // ── 2. 触点竖条（左/右竖线） ─────────────────────────────
    QPen barPen(lineColor, qMax(1.5, h * 0.065), Qt::SolidLine, Qt::FlatCap);
    painter->setPen(barPen);
    qreal barTop = h * 0.1, barBot = h * 0.9;
    painter->drawLine(QPointF(lx, barTop), QPointF(lx, barBot));
    painter->drawLine(QPointF(rx, barTop), QPointF(rx, barBot));

    // ── 3. 类型标记 ──────────────────────────────────────────
    painter->setPen(QPen(lineColor, qMax(1.0, h * 0.04)));
    switch (m_type) {
    case NormalClosed:
        painter->drawLine(QPointF(lx + 1, barBot), QPointF(rx - 1, barTop));
        break;

    case PositiveTransition:
    case NegativeTransition: {
        QRectF box(lx + 2, h * 0.15, rx - lx - 4, h * 0.7);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(box);
        QFont f; f.setPixelSize(qMax(7, (int)(h * 0.28))); f.setBold(true);
        painter->setFont(f);
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
    labelFont.setPixelSize(qMax(8, (int)(h * 0.35)));
    painter->setFont(labelFont);
    painter->setPen(selected ? QColor("#0057A8") : QColor("#333333"));
    qreal labelH = qMin(h * 0.55, 22.0);
    painter->drawText(QRectF(0, -labelH, w, labelH), Qt::AlignCenter, m_tagName);
}

QPointF ContactItem::leftPort()  const { return mapToScene(0,    m_h / 2.0); }
QPointF ContactItem::rightPort() const { return mapToScene(m_w,  m_h / 2.0); }

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
