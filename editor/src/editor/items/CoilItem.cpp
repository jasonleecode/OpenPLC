// 线圈

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QInputDialog>

#include "CoilItem.h"

CoilItem::CoilItem(QGraphicsItem *parent)
    : BaseItem(parent), m_tagName("Y0.0")
{
    setToolTip("Coil Output (Output Relay)");
}

QRectF CoilItem::boundingRect() const {
    return QRectF(0, -20, m_width, m_height + 20);
}

void CoilItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget);

    // 1. 设置画笔
    QColor penColor = (option->state & QStyle::State_Selected) ? QColor("#FFA500") : Qt::white;
    painter->setPen(QPen(penColor, 2, Qt::SolidLine));

    // 2. 绘制左右引脚 (线)
    // 左边留空一点给弧线，从 0 到 15
    painter->drawLine(0, 20, 15, 20);
    // 右边从 45 到 60
    painter->drawLine(45, 20, 60, 20);

    // 3. 绘制弧线括号 ( )
    // 左弧 (
    painter->drawArc(10, 5, 10, 30, 90 * 16, 180 * 16); // Qt的角度单位是 1/16 度
    // 右弧 )
    painter->drawArc(40, 5, 10, 30, 90 * 16, -180 * 16);

    // 4. 绘制文字
    painter->setPen(QColor("#AAAAAA"));
    painter->drawText(QRectF(0, -20, 60, 20), Qt::AlignCenter, m_tagName);
}

QPointF CoilItem::leftPort() const { return mapToScene(0, 20); }
QPointF CoilItem::rightPort() const { return mapToScene(60, 20); }

void CoilItem::setTagName(const QString &name) {
    m_tagName = name;
    update();
}

void CoilItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event);
    
    bool ok;
    QString text = QInputDialog::getText(nullptr, "编辑线圈",
                                         "变量名称 (例如 Y0):", QLineEdit::Normal,
                                         m_tagName, &ok);
    if (ok && !text.isEmpty()) {
        setTagName(text);
    }
}