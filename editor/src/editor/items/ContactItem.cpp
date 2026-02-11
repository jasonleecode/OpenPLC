// 触点 (常开/常闭)

#include <QPainter>
#include <QInputDialog>
#include <QStyleOptionGraphicsItem>

#include "ContactItem.h"

ContactItem::ContactItem(ContactType type, QGraphicsItem *parent)
    : BaseItem(parent), m_type(type), m_tagName("??")
{
    // 设置工具提示，鼠标悬停时显示
    setToolTip("PLC Contact (Click to select, Drag to move)");
}

QRectF ContactItem::boundingRect() const
{
    // 定义点击区域：稍微比实际画的大一点点，防止甚至包含文字区域
    // (x, y, w, h) -> 左上角在 (0,0)，向右下延伸
    return QRectF(0, -20, m_width, m_height + 20); 
}

void ContactItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget);

    // 1. 设置画笔
    // 如果被选中，线变成橙色且加粗；否则是白色（配合深色背景）
    QColor penColor = (option->state & QStyle::State_Selected) ? QColor("#FFA500") : Qt::white;
    painter->setPen(QPen(penColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    // 2. 绘制左右引脚 (横线)
    // 左引脚: (0, 20) -> (20, 20)
    painter->drawLine(0, 20, 20, 20);
    // 右引脚: (40, 20) -> (60, 20)
    painter->drawLine(40, 20, 60, 20);

    // 3. 绘制触点符号 |  |
    // 左竖线
    painter->drawLine(20, 5, 20, 35);
    // 右竖线
    painter->drawLine(40, 5, 40, 35);

    // 4. 如果是常闭 (NC)，画中间的斜杠
    if (m_type == NormalClosed) {
        painter->drawLine(21, 35, 39, 5);
    }

    // 5. 绘制标签文字 (位于元件上方)
    // 字体颜色稍微暗一点
    painter->setPen(QColor("#AAAAAA"));
    QFont font = painter->font();
    font.setPointSize(10);
    painter->setFont(font);
    
    // 在 (0, -20) 到 (60, 15) 的矩形区域居中绘制文字
    painter->drawText(QRectF(0, -20, 60, 20), Qt::AlignCenter, m_tagName);
}

QPointF ContactItem::leftPort() const {
    // 映射局部坐标到场景坐标
    return mapToScene(0, 20); 
}

QPointF ContactItem::rightPort() const {
    return mapToScene(60, 20);
}

void ContactItem::setTagName(const QString &name) {
    m_tagName = name;
    update(); // 触发重绘
}

void ContactItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event);
    
    bool ok;
    QString text = QInputDialog::getText(nullptr, "编辑触点",
                                         "变量名称 (例如 X0.1):", QLineEdit::Normal,
                                         m_tagName, &ok);
    if (ok && !text.isEmpty()) {
        setTagName(text);
    }
}