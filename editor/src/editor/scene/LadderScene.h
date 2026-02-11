#pragma once
#include <QGraphicsScene>

#include "../items/WireItem.h"

enum EditorMode {
    Mode_Select,        // 选择/拖拽模式
    Mode_AddContact_NO, // 添加常开触点
    Mode_AddContact_NC, // 添加常闭触点
    Mode_AddCoil,        // 添加线圈
    Mode_AddWire        // 画线模式
};

class LadderScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit LadderScene(QObject *parent = nullptr);

    // 设置网格大小，通常 PLC 梯形图用 20px 或 30px
    static const int GridSize = 20;

    // 设置当前模式
    void setMode(EditorMode mode);

protected:
    // 重写绘制背景函数，这是高性能网格的关键
    void drawBackground(QPainter *painter, const QRectF &rect) override;

    // 处理鼠标点击事件
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

    // 处理鼠标移动事件 (画线模式)
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    
private:
    EditorMode m_mode; // 当前模式

    QColor m_gridColor;
    QColor m_gridColorFine;
    QColor m_backgroundColor;

    // 临时导线 (正在画的那根)
    WireItem* m_tempWire = nullptr;
};