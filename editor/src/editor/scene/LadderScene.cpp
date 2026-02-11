// 自定义场景 (处理网格、拖拽逻辑)

#include <QPainter>
#include <QtMath>
#include <QVarLengthArray>
#include <QGraphicsSceneMouseEvent>

#include "LadderScene.h"
#include "../items/ContactItem.h"
#include "../items/CoilItem.h"


LadderScene::LadderScene(QObject *parent)
    : QGraphicsScene(parent), m_mode(Mode_Select)
{
    // 1. 设置场景为一个巨大的区域 (模拟无限画布)
    // 这里的坐标范围 (-50000, -50000) 到 (50000, 50000)
    setSceneRect(-50000, -50000, 100000, 100000);

    // 2. 初始化颜色 (深色工业风)
    m_backgroundColor = QColor("#282828"); // 深灰背景
    m_gridColor = QColor("#353535");       // 普通网格线
    m_gridColorFine = QColor("#454545");   // 粗网格线 (每10格)
}

void LadderScene::setMode(EditorMode mode) {
    m_mode = mode;
}

void LadderScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    // 1. 填充背景色
    painter->fillRect(rect, m_backgroundColor);

    // 2. 计算当前可见区域的边界
    qreal left = rect.left();
    qreal top = rect.top();
    qreal right = rect.right();
    qreal bottom = rect.bottom();

    // 3. 对齐到网格边界 (这是为了防止拖动时网格抖动)
    // qFloor 向下取整
    int firstLeft = left - (int(left) % GridSize);
    int firstTop = top - (int(top) % GridSize);

    // 4. 收集线条 (使用 QVarLengthArray 比 QVector 更快，避免堆分配)
    QVarLengthArray<QLineF, 100> gridLines;
    QVarLengthArray<QLineF, 100> fineLines;

    // --- 绘制垂直线 ---
    for (qreal x = firstLeft; x <= right; x += GridSize) {
        QLineF line(x, top, x, bottom);
        if (int(x) % (GridSize * 10) == 0) // 每10格画一条粗线
            fineLines.append(line);
        else
            gridLines.append(line);
    }

    // --- 绘制水平线 ---
    for (qreal y = firstTop; y <= bottom; y += GridSize) {
        QLineF line(left, y, right, y);
        if (int(y) % (GridSize * 10) == 0)
            fineLines.append(line);
        else
            gridLines.append(line);
    }

    // 5. 实际绘制
    painter->setPen(QPen(m_gridColor, 1.0));
    painter->drawLines(gridLines.data(), gridLines.size());

    painter->setPen(QPen(m_gridColorFine, 2.0)); // 粗线用2px
    painter->drawLines(fineLines.data(), fineLines.size());
}

void LadderScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    // 只有左键点击才处理添加逻辑
    if (event->button() != Qt::LeftButton) {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    // 如果是选择模式，直接交给基类处理 (选中、拖拽)
    if (m_mode == Mode_Select) {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    // === 画线逻辑 ===
    if (m_mode == Mode_AddWire) {
        // 计算吸附后的坐标
        QPointF scenePos = event->scenePos();
        qreal x = round(scenePos.x() / GridSize) * GridSize;
        qreal y = round(scenePos.y() / GridSize) * GridSize;
        QPointF snapPos(x, y);

        if (m_tempWire == nullptr) {
            // [阶段1] 第一次点击：开始画线
            // 起点和终点先设为一样
            m_tempWire = new WireItem(snapPos, snapPos);
            addItem(m_tempWire);
        } else {
            // [阶段2] 第二次点击：结束画线
            m_tempWire->setEndPos(snapPos);
            m_tempWire = nullptr; // 指针置空，准备画下一根
        }
        return; // 处理完了，不传给基类
    }

    // === 添加元件逻辑 ===
    BaseItem* newItem = nullptr;
    
    switch (m_mode) {
    case Mode_AddContact_NO:
        newItem = new ContactItem(ContactItem::NormalOpen);
        static int noCount = 0;
        // 动态转类型设置名字 (简单的演示)
        ((ContactItem*)newItem)->setTagName(QString("X%1").arg(noCount++));
        break;
        
    case Mode_AddContact_NC:
        newItem = new ContactItem(ContactItem::NormalClosed);
        static int ncCount = 0;
        ((ContactItem*)newItem)->setTagName(QString("X%1").arg(ncCount++));
        break;
        
    case Mode_AddCoil:
        newItem = new CoilItem();
        static int coilCount = 0;
        ((CoilItem*)newItem)->setTagName(QString("Y%1").arg(coilCount++));
        break;
        
    default:
        break;
    }

    if (newItem) {
        // 1. 设置位置 (鼠标点哪里就放哪里，Item内部会自动吸附网格)
        newItem->setPos(event->scenePos());
        
        // 2. 添加到场景
        addItem(newItem);
        
        // 3. 体验优化：添加完后，最好还是停留在添加模式，方便连续放置
        // 如果想添加一个就自动切回选择模式，可以在这里调用 setMode(Mode_Select);
    }
}

// 鼠标移动事件
void LadderScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    // 必须调用基类，否则拖拽选择框会失效
    QGraphicsScene::mouseMoveEvent(event);

    // 如果正在画线，更新线的终点
    if (m_mode == Mode_AddWire && m_tempWire != nullptr) {
        QPointF scenePos = event->scenePos();
        // 同样要吸附网格，这样看起来更整齐
        qreal x = round(scenePos.x() / GridSize) * GridSize;
        qreal y = round(scenePos.y() / GridSize) * GridSize;
        
        m_tempWire->setEndPos(QPointF(x, y));
    }
}