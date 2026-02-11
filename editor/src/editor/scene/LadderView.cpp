// 自定义视图 (处理缩放、平移)

#include "LadderView.h"
#include <QWheelEvent>
#include <QScrollBar>

LadderView::LadderView(QWidget *parent)
    : QGraphicsView(parent)
{
    // 1. 渲染优化
    setRenderHint(QPainter::Antialiasing); // 抗锯齿
    setRenderHint(QPainter::TextAntialiasing);
    
    // 2. 视口更新模式 (FullViewportUpdate 最慢但最安全，SmartViewportUpdate 适合图元多的时候)
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    
    // 3. 隐藏滚动条 (像 CAD 一样无限漫游)
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // 4. 变换锚点 (缩放时以鼠标为中心)
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    
    // 5. 允许拖拽模式
    setDragMode(QGraphicsView::RubberBandDrag); // 默认左键是框选
}

void LadderView::wheelEvent(QWheelEvent *event)
{
    // Ctrl + 滚轮 = 缩放
    if (event->modifiers() & Qt::ControlModifier) {
        double angle = event->angleDelta().y();
        double factor = (angle > 0) ? 1.1 : 0.9;
        
        // 限制缩放比例 (比如 0.1x 到 5.0x)
        const qreal currentScale = transform().m11();
        if ((factor < 1 && currentScale < 0.1) || (factor > 1 && currentScale > 5.0))
            return;

        scale(factor, factor);
        event->accept();
    } else {
        // 普通滚轮 = 垂直滚动 (也可以改成缩放，看你习惯)
        QGraphicsView::wheelEvent(event);
    }
}

void LadderView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        setDragMode(QGraphicsView::ScrollHandDrag);
        
        // Qt6 新构造函数: (Type, LocalPos, GlobalPos, Button, Buttons, Modifiers)
        QMouseEvent fakeEvent(QEvent::MouseButtonPress,
                              event->position(),       // 本地坐标 (QPointF)
                              event->globalPosition(), // 全局坐标 (QPointF)
                              Qt::LeftButton,
                              Qt::LeftButton,
                              event->modifiers());

        QGraphicsView::mousePressEvent(&fakeEvent);
        event->accept();
    } else {
        QGraphicsView::mousePressEvent(event);
    }
}

void LadderView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        setDragMode(QGraphicsView::RubberBandDrag);
        
        QMouseEvent fakeEvent(QEvent::MouseButtonRelease,
                              event->position(),
                              event->globalPosition(),
                              Qt::LeftButton,
                              Qt::LeftButton,
                              event->modifiers());

        QGraphicsView::mouseReleaseEvent(&fakeEvent);
        event->accept();
    } else {
        QGraphicsView::mouseReleaseEvent(event);
    }
}