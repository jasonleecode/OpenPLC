#pragma once
#include <QDomElement>
#include <QDomDocument>
#include <QMap>
#include <QList>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QtGlobal>       // qQNaN / qIsNaN

// LadderScene 是基类，同时提供 EditorMode、编辑状态、undo stack
#include "LadderScene.h"

class QGraphicsPathItem;
class QGraphicsTextItem;

// ─────────────────────────────────────────────────────────────
// PlcOpenViewer — PLCopen XML 图形编辑 / 查看场景
//
//   继承 LadderScene，在其基础上增加：
//   • loadFromXmlString() — 从 PLCopen XML 导入 LD/FBD/SFC
//   • initEmpty()         — 新建空白画布（新 LD/FBD 程序）
//   • toXmlString()       — 场景 → 更新后的 PLCopen body XML
//
//   编辑模式（setMode/currentMode/modeChanged）、撤销/重做（undoStack）
//   以及所有鼠标/键盘事件处理均继承自 LadderScene。
//   仅覆盖 drawBackground：使用简单点阵（无 LD 电源母线）。
// ─────────────────────────────────────────────────────────────
class PlcOpenViewer : public LadderScene
{
    Q_OBJECT
public:
    explicit PlcOpenViewer(QObject* parent = nullptr);

    // 从 PLCopen XML 字符串加载（含 "LD\n"/"FBD\n"/"SFC\n" 前缀）
    void loadFromXmlString(const QString& xmlBody);
    // 初始化为空的可编辑画布（新建 LD / FBD 程序时使用）
    void initEmpty(const QString& lang = "LD");
    // 序列化场景 → "FBD\n<FBD>...</FBD>"（同步 item 位置后返回）
    QString toXmlString();

    // setMode / currentMode / undoStack / modeChanged 均继承自 LadderScene

protected:
    // 覆盖背景绘制：简单点阵（无 LD 电源母线），Beremiz 风格
    void drawBackground(QPainter* painter, const QRectF& rect) override;

    // ── 导线编辑事件 ──────────────────────────────────────────
    void mousePressEvent  (QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent   (QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void keyPressEvent    (QKeyEvent* event) override;
    void drawForeground   (QPainter* painter, const QRectF& rect) override;

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

    // ── 动态导线（FBD/LD 连线路由）───────────────────────────
    struct FbdConn {
        int    srcId;    QString srcParam;  // 源输出端口 localId + 参数名
        int    dstId;    QString dstParam;  // 目标输入端口 localId + 参数名
        QGraphicsPathItem* wire = nullptr;  // 可视导线（由 scene 管理生命周期）
        qreal  customMidX = qQNaN();        // NaN=自动取中点; 否则为用户拖拽设置的折点 x
        qreal  srcJogY    = qQNaN();        // NaN=端口同高; 否则上段水平线 y（在 src 侧加垂直折）
        qreal  dstJogY    = qQNaN();        // NaN=端口同高; 否则下段水平线 y（在 dst 侧加垂直折）
    };

    // 端口反向查找结果（场景坐标 → 最近的 item 端口）
    struct PortRef {
        int     lid      = -1;
        QString param;
        bool    isOutput = false;
        bool    valid()  const { return lid >= 0; }
    };

    void    updateAllWires();
    void    syncPositionsToDoc();       // item 坐标 → m_bodyDoc <position>
    void    syncWirePathsToDoc();       // 导线折点 → m_bodyDoc <connection>/<position>
    void    buildBodyFromScene();       // 从场景 item 重建 m_bodyDoc（新建画布用）

    QPointF getOutputPortScene(int lid, const QString& param) const;
    QPointF getInputPortScene (int lid, const QString& param) const;
    static QDomElement findElemById(const QDomElement& root, int lid);

    // 端口反向查找
    PortRef findPortAt(const QPointF& pos, qreal radius = 20.0) const;
    // 判断 pos 是否在导线 c 的垂直线段上（返回 true 可左右拖拽）
    bool nearWireVertSeg (const FbdConn& c, const QPointF& pos, qreal tol = 5.0) const;
    // 判断 pos 是否在导线 c 的水平线段上：0=否, 1=src侧, 2=dst侧（可上下拖拽）
    int  nearWireHorizSeg(const FbdConn& c, const QPointF& pos, qreal tol = 5.0) const;

    // localId → output port 场景坐标（PLCopen 加载时快照，SFC/兼容用）
    QMap<int, QPointF>               m_outPort;
    QMap<int, QMap<QString,QPointF>> m_namedOutPort;

    // m_items (localId → scene item) 继承自 LadderScene::m_items

    // FBD/LD 导线连接表
    QList<FbdConn> m_connections;
    bool           m_updatingWires = false;
    QTimer*        m_wireTimer     = nullptr;

    // ── 导线线段拖拽状态 ──────────────────────────────────────
    int   m_segDragIdx    = -1;   // 正在拖拽的 m_connections 索引（-1=未激活）
    qreal m_segDragOldMidX = 0.0; // 拖拽前 midX（用于 undo）

    // ── 水平线段拖拽状态（上下移动） ─────────────────────────
    int   m_horizDragIdx   = -1;   // 正在拖拽的 m_connections 索引（-1=未激活）
    bool  m_horizDragIsSrc = true; // true=src侧水平段, false=dst侧水平段
    qreal m_horizDragOldY  = 0.0;  // 拖拽前 jogY（用于 undo）

    // ── 导线端点拖拽状态 ──────────────────────────────────────
    int     m_epDragIdx   = -1;   // 正在拖拽的 m_connections 索引（-1=未激活）
    bool    m_epDragIsSrc = false;// true=拖拽源端点, false=拖拽目标端点
    FbdConn m_epDragOldConn;      // 拖拽前的连接（用于 undo / 恢复）

    // m_nextLocalId 继承自 LadderScene::m_nextLocalId

    // 可修改的 body DOM（用于序列化）
    QString      m_bodyLanguage;       // "FBD" / "LD" / "SFC"
    QDomDocument m_bodyDoc;
    bool         m_isNewScene = false; // true = initEmpty() 创建，需 buildBodyFromScene()

    // ── 折线画线模式折点积累 ──────────────────────────────────
    QList<QPointF> m_wirePoints;  // 已点击固定的折点（第一点到倒数第二点）

    // m_mode / m_tempWire / m_portSnapPos / m_showPortSnap / counters 继承自 LadderScene
    // m_undoStack / m_dragStartPos 继承自 LadderScene
    // GridSize 继承自 LadderScene

    // file-local undo commands 的友元声明
    friend class AddWireCmd;
    friend class MoveWireEndpointCmd;
    friend class MoveWireSegmentCmd;
    friend class MoveWireHorizCmd;
};
