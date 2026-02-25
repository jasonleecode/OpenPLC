#pragma once
#include <QGraphicsScene>
#include <QDomElement>
#include <QDomDocument>
#include <QHash>
#include <QMap>
#include <QList>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QUndoStack>

// EditorMode 定义在 LadderScene.h，两个场景共用同一套工具枚举
#include "LadderScene.h"

class WireItem;
class BaseItem;

// ─────────────────────────────────────────────────────────────
// PlcOpenViewer — 统一的图形编辑 / 查看场景
//   • loadFromXmlString() — 从 PLCopen XML 导入 LD/FBD/SFC
//   • initEmpty()         — 新建空白画布（新 LD/FBD 程序）
//   • setMode()           — 工具栏驱动编辑模式
//   • toXmlString()       — 场景 → 更新后的 PLCopen body XML
// ─────────────────────────────────────────────────────────────
class PlcOpenViewer : public QGraphicsScene
{
    Q_OBJECT
public:
    explicit PlcOpenViewer(QObject* parent = nullptr);

    // 从 PLCopen XML 字符串加载（含 "LD\n"/"FBD\n"/"SFC\n" 前缀）
    void loadFromXmlString(const QString& xmlBody);
    // 初始化为空的可编辑画布（新建 LD / FBD 程序时使用）
    // lang: "LD" / "FBD" / "SFC"
    void initEmpty(const QString& lang = "LD");

    // 序列化场景 → "FBD\n<FBD>...</FBD>"（同步 item 位置后返回）
    QString toXmlString();

    // ── 编辑模式（供工具栏连接）──────────────────────────────
    void       setMode(EditorMode mode);
    EditorMode currentMode() const { return m_mode; }

    // ── 撤销/重做 ─────────────────────────────────────────────
    QUndoStack* undoStack() const { return m_undoStack; }

signals:
    void modeChanged(EditorMode mode);

protected:
    void drawBackground  (QPainter* painter, const QRectF& rect) override;
    void drawForeground  (QPainter* painter, const QRectF& rect) override;
    void mousePressEvent (QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent  (QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void keyPressEvent   (QKeyEvent* event) override;
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

    // ── 动态导线 ─────────────────────────────────────────────
    struct FbdConn {
        int    srcId;    QString srcParam;  // 源输出端口 localId + 参数名
        int    dstId;    QString dstParam;  // 目标输入端口 localId + 参数名
        QGraphicsPathItem* wire = nullptr;  // 可视导线（由 scene 管理生命周期）
    };
    void    updateAllWires();
    void    syncPositionsToDoc();       // item 坐标 → m_bodyDoc <position>
    void    syncWirePathsToDoc();       // 导线折点 → m_bodyDoc <connection>/<position>
    void    buildBodyFromScene();       // 从场景 item 重新构建 m_bodyDoc（新建画布用）

    QPointF getOutputPortScene(int lid, const QString& param) const;
    QPointF getInputPortScene (int lid, const QString& param) const;
    static QDomElement findElemById(const QDomElement& root, int lid);

    // localId → output port 场景坐标（加载时的快照，用于 SFC/兼容）
    QMap<int, QPointF>               m_outPort;
    QMap<int, QMap<QString,QPointF>> m_namedOutPort;
    QMap<int, QGraphicsItem*>        m_items;   // localId → scene item

    // FBD/LD 导线连接表
    QList<FbdConn> m_connections;
    bool           m_updatingWires = false;
    QTimer*        m_wireTimer     = nullptr;
    int            m_nextLocalId   = 10000;   // 新增 item 的 localId 起始值

    // 可修改的 body DOM（用于序列化）
    QString      m_bodyLanguage;       // "FBD" / "LD" / "SFC"
    QDomDocument m_bodyDoc;
    bool         m_isNewScene = false; // true = initEmpty() 创建，需 buildBodyFromScene()

    // ── 编辑状态 ──────────────────────────────────────────────
    EditorMode m_mode         = Mode_Select;
    WireItem*  m_tempWire     = nullptr;
    QPointF    m_portSnapPos;
    bool       m_showPortSnap = false;
    int        m_contactCount = 0;
    int        m_coilCount    = 0;
    int        m_fbCount      = 0;

    // ── 撤销/重做 ─────────────────────────────────────────────
    QUndoStack*                    m_undoStack    = nullptr;
    QHash<QGraphicsItem*, QPointF> m_dragStartPos; // 拖拽开始前的位置快照

    static constexpr int GridSize = 20;
};
