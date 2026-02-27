#pragma once
#include <QGraphicsView>
#include "LadderScene.h"  // for EditorMode

class LadderView : public QGraphicsView {
    Q_OBJECT
public:
    explicit LadderView(QWidget *parent = nullptr);

public slots:
    // 由 LadderScene::modeChanged 驱动，切换鼠标光标
    void onModeChanged(EditorMode mode);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};
