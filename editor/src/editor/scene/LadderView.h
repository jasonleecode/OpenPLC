#pragma once
#include <QGraphicsView>

class LadderView : public QGraphicsView {
    Q_OBJECT
public:
    explicit LadderView(QWidget *parent = nullptr);

protected:
    // 处理鼠标滚轮缩放
    void wheelEvent(QWheelEvent *event) override;
    
    // 处理鼠标中键平移 (按下中键拖动)
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void setupMatrix();
};