#pragma once
#include <QGraphicsScene>
#include <QDomElement>
#include <QMap>
#include <QPointF>
#include <QString>

// EditorMode 定义在 LadderScene.h，两个场景共用同一套工具枚举
#include "LadderScene.h"

class WireItem;
class BaseItem;

// ─────────────────────────────────────────────────────────────
// PlcOpenViewer — 统一的图形编辑 / 查看场景
//   • loadFromXmlString() — 从 PLCopen XML 导入 LD/FBD/SFC
//   • initEmpty()         — 新建空白画布（新 LD/FBD 程序）
//   • setMode()           — 工具栏驱动编辑模式
// ─────────────────────────────────────────────────────────────
class PlcOpenViewer : public QGraphicsScene
{
    Q_OBJECT
public:
    explicit PlcOpenViewer(QObject* parent = nullptr);

    // 从 PLCopen XML 字符串加载（含 "LD\n"/"FBD\n"/"SFC\n" 前缀）
    void loadFromXmlString(const QString& xmlBody);
    // 初始化为空的可编辑画布（新建 LD / FBD 程序时使用）
    void initEmpty();

    // ── 编辑模式（供工具栏连接）──────────────────────────────
    void       setMode(EditorMode mode);
    EditorMode currentMode() const { return m_mode; }

signals:
    void modeChanged(EditorMode mode);

protected:
    void drawBackground (QPainter* painter, const QRectF& rect) override;
    void drawForeground (QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent (QGraphicsSceneMouseEvent* event) override;
    void keyPressEvent  (QKeyEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    // ── PLCopen XML 渲染 ──────────────────────────────────────
    void buildFbd(const QDomElement& body);
    void buildSfc(const QDomElement& body);
    void createFbdItems(const QDomElement& body);
    void drawFbdWires  (const QDomElement& body);
    void createSfcItems(const QDomElement& body);
    void drawSfcWires  (const QDomElement& body);

    // ── 辅助 ──────────────────────────────────────────────────
    QPointF            absPos(const QDomElement& elem) const;
    QGraphicsTextItem* addLabel(const QString& text, const QRectF& rect,
                                Qt::AlignmentFlag align = Qt::AlignCenter,
                                int fontSize = 9);
    QPointF snapToNearestPort(const QPointF& pos, qreal radius) const;

    // localId → output port 场景坐标（用于连线起点）
    QMap<int, QPointF>               m_outPort;
    QMap<int, QMap<QString,QPointF>> m_namedOutPort;
    QMap<int, QGraphicsItem*>        m_items;

    // ── 编辑状态 ──────────────────────────────────────────────
    EditorMode m_mode         = Mode_Select;
    WireItem*  m_tempWire     = nullptr;
    QPointF    m_portSnapPos;
    bool       m_showPortSnap = false;
    int        m_contactCount = 0;
    int        m_coilCount    = 0;
    int        m_fbCount      = 0;

    static constexpr int GridSize = 20;
};
