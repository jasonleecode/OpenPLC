// 自定义视图 (处理缩放、平移、模式光标、拖放)

#include "LadderView.h"
#include "LadderScene.h"
#include <QWheelEvent>
#include <QScrollBar>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

LadderView::LadderView(QWidget *parent)
    : QGraphicsView(parent)
{
    // 1. 渲染优化
    setRenderHint(QPainter::Antialiasing); // 抗锯齿
    setRenderHint(QPainter::TextAntialiasing);
    
    // 2. 视口更新模式 (FullViewportUpdate 最慢但最安全，SmartViewportUpdate 适合图元多的时候)
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    
    // 3. 滚动条（程序变长时可用，也支持中键平移）
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    
    // 4. 变换锚点 (缩放时以鼠标为中心)
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    
    // 5. 允许拖拽模式
    setDragMode(QGraphicsView::RubberBandDrag); // 默认左键是框选

    // 6. 接受从 Library 面板拖放的功能块
    setAcceptDrops(true);
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

// ──────────────────────────────────────────────────────────────
// 拖放处理 —— 从 Library 面板拖入功能块
// ──────────────────────────────────────────────────────────────
void LadderView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-tizi-blocktype"))
        event->acceptProposedAction();
    else
        event->ignore();
}

void LadderView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-tizi-blocktype"))
        event->acceptProposedAction();
    else
        event->ignore();
}

void LadderView::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasFormat("application/x-tizi-blocktype")) {
        event->ignore();
        return;
    }
    auto* ls = qobject_cast<LadderScene*>(scene());
    if (!ls) {
        event->ignore();
        return;
    }
    const QString typeName = QString::fromUtf8(
        event->mimeData()->data("application/x-tizi-blocktype"));
    ls->addFunctionBlock(typeName, mapToScene(event->position().toPoint()));
    event->acceptProposedAction();
}

// ──────────────────────────────────────────────────────────────
// onModeChanged —— 根据编辑模式切换鼠标光标
// ──────────────────────────────────────────────────────────────
void LadderView::onModeChanged(EditorMode mode)
{
    switch (mode) {
    case Mode_Select:
        setCursor(Qt::ArrowCursor);
        setDragMode(QGraphicsView::RubberBandDrag);
        break;
    case Mode_AddWire:
        setCursor(Qt::CrossCursor);
        setDragMode(QGraphicsView::NoDrag);
        break;
    default:  // 所有放置模式
        setCursor(Qt::CrossCursor);
        setDragMode(QGraphicsView::NoDrag);
        break;
    }
}