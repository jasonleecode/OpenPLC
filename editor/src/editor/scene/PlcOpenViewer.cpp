// PlcOpenViewer.cpp — PLCopen XML 图形编辑/查看场景
// 继承 LadderScene，增加 PLCopen XML 导入/导出及 FBD/SFC 渲染

#include "PlcOpenViewer.h"

#include "../items/ContactItem.h"
#include "../items/CoilItem.h"
#include "../items/FunctionBlockItem.h"
#include "../items/VarBoxItem.h"
#include "../items/WireItem.h"
#include "../items/BaseItem.h"

#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsTextItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QApplication>
#include <QDomDocument>
#include <QFont>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QTimer>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <algorithm>
#include "../../utils/UndoStack.h"

// ─────────────────────────────────────────────────────────────
// PLCopen 坐标 → 场景坐标放大系数
// ─────────────────────────────────────────────────────────────
static constexpr qreal kScale = 2.0;

// ═══════════════════════════════════════════════════════════════
// 导线编辑 Undo 命令（仅在本文件内可见）
// ═══════════════════════════════════════════════════════════════

// AddWireCmd —— 添加一条绑定到端口的导线（含 m_connections 更新）
class AddWireCmd : public QUndoCommand {
    PlcOpenViewer*         m_scene;
    QGraphicsPathItem*     m_wire;
    PlcOpenViewer::FbdConn m_conn;
    bool                   m_ownWire = true;
public:
    AddWireCmd(PlcOpenViewer* scene, QGraphicsPathItem* wire,
               PlcOpenViewer::FbdConn conn, QUndoCommand* parent = nullptr)
        : QUndoCommand("Add Wire", parent)
        , m_scene(scene), m_wire(wire), m_conn(conn) {}
    ~AddWireCmd() override { if (m_ownWire) delete m_wire; }
    void redo() override {
        m_conn.wire = m_wire;
        m_scene->addItem(m_wire);
        m_scene->m_connections << m_conn;
        m_scene->m_wireTimer->start();
        m_ownWire = false;
    }
    void undo() override {
        for (int i = m_scene->m_connections.size()-1; i >= 0; --i)
            if (m_scene->m_connections[i].wire == m_wire)
                m_scene->m_connections.removeAt(i);
        m_scene->removeItem(m_wire);
        m_ownWire = true;
    }
};

// MoveWireEndpointCmd —— 重连导线端点（切换所连的端口）
class MoveWireEndpointCmd : public QUndoCommand {
    PlcOpenViewer*         m_scene;
    QGraphicsPathItem*     m_wire;
    PlcOpenViewer::FbdConn m_before, m_after;
public:
    MoveWireEndpointCmd(PlcOpenViewer* scene, QGraphicsPathItem* wire,
                        const PlcOpenViewer::FbdConn& before,
                        const PlcOpenViewer::FbdConn& after,
                        QUndoCommand* parent = nullptr)
        : QUndoCommand("Reconnect Wire", parent)
        , m_scene(scene), m_wire(wire), m_before(before), m_after(after) {}
    void redo() override {
        for (auto& c : m_scene->m_connections)
            if (c.wire == m_wire) { auto* w = c.wire; c = m_after; c.wire = w; break; }
        m_scene->updateAllWires();
    }
    void undo() override {
        for (auto& c : m_scene->m_connections)
            if (c.wire == m_wire) { auto* w = c.wire; c = m_before; c.wire = w; break; }
        m_scene->updateAllWires();
    }
};

// MoveWireHorizCmd —— 拖动导线水平线段（上下移动，更新 srcJogY 或 dstJogY）
class MoveWireHorizCmd : public QUndoCommand {
    PlcOpenViewer*     m_scene;
    QGraphicsPathItem* m_wire;
    bool               m_isSrc;    // true=srcJogY, false=dstJogY
    qreal              m_before, m_after;
public:
    MoveWireHorizCmd(PlcOpenViewer* scene, QGraphicsPathItem* wire,
                     bool isSrc, qreal before, qreal after,
                     QUndoCommand* parent = nullptr)
        : QUndoCommand("Move Wire Segment", parent)
        , m_scene(scene), m_wire(wire), m_isSrc(isSrc)
        , m_before(before), m_after(after) {}
    void redo() override {
        for (auto& c : m_scene->m_connections)
            if (c.wire == m_wire) {
                if (m_isSrc) c.srcJogY = m_after; else c.dstJogY = m_after;
                break;
            }
        m_scene->updateAllWires();
    }
    void undo() override {
        for (auto& c : m_scene->m_connections)
            if (c.wire == m_wire) {
                if (m_isSrc) c.srcJogY = m_before; else c.dstJogY = m_before;
                break;
            }
        m_scene->updateAllWires();
    }
};

// MoveWireSegmentCmd —— 拖动导线中间折点（调整走线，仅更新 customMidX）
class MoveWireSegmentCmd : public QUndoCommand {
    PlcOpenViewer*     m_scene;
    QGraphicsPathItem* m_wire;
    qreal              m_before, m_after;
public:
    MoveWireSegmentCmd(PlcOpenViewer* scene, QGraphicsPathItem* wire,
                       qreal before, qreal after,
                       QUndoCommand* parent = nullptr)
        : QUndoCommand("Move Wire Segment", parent)
        , m_scene(scene), m_wire(wire), m_before(before), m_after(after) {}
    void redo() override {
        for (auto& c : m_scene->m_connections)
            if (c.wire == m_wire) { c.customMidX = m_after; break; }
        m_scene->updateAllWires();
    }
    void undo() override {
        for (auto& c : m_scene->m_connections)
            if (c.wire == m_wire) { c.customMidX = m_before; break; }
        m_scene->updateAllWires();
    }
};

// ─────────────────────────────────────────────────────────────
// SFC 颜色（FBD/LD 用各 item 自身配色）
// ─────────────────────────────────────────────────────────────
static const QColor kColStep     { "#6A1B9A" };
static const QColor kColStepFill { "#F3E5F5" };
static const QColor kColTrans    { "#1A2E4A" };
static const QColor kColBlock    { "#1A2E4A" };
static const QColor kColWire     { "#1A2E4A" };

PlcOpenViewer::PlcOpenViewer(QObject* parent)
    : LadderScene(parent)   // 继承自 LadderScene（包含编辑状态、undo stack）
{
    setSceneRect(-80, -80, 2200, 2000);

    m_wireTimer = new QTimer(this);
    m_wireTimer->setSingleShot(true);
    m_wireTimer->setInterval(0);
    connect(m_wireTimer, &QTimer::timeout,
            this, &PlcOpenViewer::updateAllWires);

    // m_undoStack 已由 LadderScene 构造函数创建
}

// ─────────────────────────────────────────────────────────────
// 背景绘制：简单点阵（无 LD 电源母线），覆盖 LadderScene 默认
// ─────────────────────────────────────────────────────────────
void PlcOpenViewer::drawBackground(QPainter* painter, const QRectF& rect)
{
    const QColor bg      = QApplication::palette().base().color();
    const QColor dotColor = bg.lightnessF() > 0.5
                            ? QColor("#CCCCCC")
                            : QColor("#3A3A3A");
    painter->fillRect(rect, bg);

    painter->setPen(QPen(dotColor, 1.0));
    const int dot = GridSize;
    int fx = (int)rect.left()  - ((int)rect.left()  % dot);
    int fy = (int)rect.top()   - ((int)rect.top()   % dot);
    for (int x = fx; x <= (int)rect.right();  x += dot)
        for (int y = fy; y <= (int)rect.bottom(); y += dot)
            painter->drawPoint(x, y);
}

// ─────────────────────────────────────────────────────────────
void PlcOpenViewer::loadFromXmlString(const QString& xmlBody)
{
    // 先清理连接表（wire 指针即将因 clear() 失效）
    m_connections.clear();
    clear();
    m_undoStack->clear();
    m_outPort.clear();
    m_namedOutPort.clear();
    m_items.clear();
    m_isNewScene = false;   // PLCopen 加载的场景，走 syncPositionsToDoc 路径

    int nl = xmlBody.indexOf('\n');
    m_bodyLanguage = xmlBody.left(nl).trimmed().toUpper();
    const QString xml  = xmlBody.mid(nl + 1);

    m_bodyDoc = QDomDocument();
    if (!m_bodyDoc.setContent(xml)) {
        addText("[ failed to parse PLCopen XML ]");
        return;
    }

    QDomElement body = m_bodyDoc.documentElement();
    if (body.isNull()) {
        addText("[ empty body ]");
        return;
    }

    if (m_bodyLanguage == "SFC")
        buildSfc(body);
    else
        buildFbd(body);

    // 根据实际内容更新 sceneRect
    QRectF bounds = itemsBoundingRect();
    if (!bounds.isEmpty())
        setSceneRect(bounds.adjusted(-80, -80, 80, 80));
}

// ─────────────────────────────────────────────────────────────
// 辅助：elem 的 <position> 的场景坐标（× kScale）
// ─────────────────────────────────────────────────────────────
QPointF PlcOpenViewer::absPos(const QDomElement& elem) const
{
    QDomElement pos = elem.firstChildElement("position");
    return { pos.attribute("x","0").toDouble() * kScale,
             pos.attribute("y","0").toDouble() * kScale };
}

// ═══════════════════════════════════════════════════════════════
// FBD / LD 渲染
// ═══════════════════════════════════════════════════════════════
void PlcOpenViewer::buildFbd(const QDomElement& body)
{
    createFbdItems(body);
    drawFbdWires(body);
}

// ─────────────────────────────────────────────────────────────
// 辅助：从 connectionPointOut / connectionPointIn 的 relPosition 得到绝对场景坐标
// ─────────────────────────────────────────────────────────────
static QPointF cpRelScene(const QDomElement& cpElem, const QPointF& elemPos)
{
    QDomElement rel = cpElem.firstChildElement("relPosition");
    return elemPos + QPointF(rel.attribute("x","0").toDouble() * kScale,
                             rel.attribute("y","0").toDouble() * kScale);
}

void PlcOpenViewer::createFbdItems(const QDomElement& body)
{
    for (QDomElement e = body.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement())
    {
        const QString tag = e.tagName();
        int    lid = e.attribute("localId", "-1").toInt();
        QPointF p  = absPos(e);
        qreal xw = e.attribute("width",  "80").toDouble() * kScale;
        qreal xh = e.attribute("height", "30").toDouble() * kScale;

        // ── 功能块（block）──────────────────────────────────────
        if (tag == "block") {
            const QString typeName = e.attribute("typeName");
            // 若 PLCopen XML 中没有 instanceName（标准函数常见），自动生成唯一名
            QString instanceName = e.attribute("instanceName");
            if (instanceName.isEmpty())
                instanceName = QString("%1_%2").arg(typeName).arg(m_fbCount++);

            QStringList      inNames, outNames;
            QVector<QPointF> inRelPts, outRelPts;

            QDomElement inVarsEl = e.firstChildElement("inputVariables");
            QDomNodeList inListEl = inVarsEl.elementsByTagName("variable");
            for (int k = 0; k < inListEl.count(); ++k) {
                QDomElement iv = inListEl.at(k).toElement();
                inNames << iv.attribute("formalParameter");
                QDomElement cpIn = iv.firstChildElement("connectionPointIn");
                QDomElement rel  = cpIn.firstChildElement("relPosition");
                inRelPts << QPointF(rel.attribute("x","0").toDouble() * kScale,
                                    rel.attribute("y","0").toDouble() * kScale);
            }

            QDomElement outVarsEl = e.firstChildElement("outputVariables");
            QDomNodeList outListEl = outVarsEl.elementsByTagName("variable");
            for (int k = 0; k < outListEl.count(); ++k) {
                QDomElement ov = outListEl.at(k).toElement();
                outNames << ov.attribute("formalParameter");
                QDomElement cpOut = ov.firstChildElement("connectionPointOut");
                QDomElement rel   = cpOut.firstChildElement("relPosition");
                outRelPts << QPointF(rel.attribute("x","0").toDouble() * kScale,
                                     rel.attribute("y","0").toDouble() * kScale);
            }

            auto* fb = new FunctionBlockItem(typeName, instanceName);
            fb->setCustomPorts(inNames, outNames);
            fb->setXmlGeometry(xw, xh, inRelPts, outRelPts);
            fb->setData(0, lid);
            fb->setPos(p);
            addItem(fb);
            m_items[lid] = fb;

            for (int k = 0; k < outNames.size(); ++k) {
                QPointF portScene = p + outRelPts[k];
                m_namedOutPort[lid][outNames[k]] = portScene;
                if (k == 0) m_outPort[lid] = portScene;
            }
        }

        // ── contact（触点）─────────────────────────────────────
        else if (tag == "contact") {
            const QString varName = e.firstChildElement("variable").text();
            bool negated = (e.attribute("negated","false") == "true");

            auto* ct = new ContactItem(
                negated ? ContactItem::NormalClosed : ContactItem::NormalOpen);
            ct->setTagName(varName);
            ct->setExplicitSize(xw, xh);
            ct->setData(0, lid);
            ct->setPos(p);
            addItem(ct);
            m_items[lid] = ct;

            QDomElement cpOut = e.firstChildElement("connectionPointOut");
            m_outPort[lid] = cpRelScene(cpOut, p);
        }

        // ── inVariable (输入变量/常量)──────────────────────────
        else if (tag == "inVariable") {
            const QString expr = e.firstChildElement("expression").text();

            auto* vb = new VarBoxItem(expr, VarBoxItem::InVar);
            vb->setExplicitSize(xw, xh);
            vb->setData(0, lid);
            vb->setPos(p);
            addItem(vb);
            m_items[lid] = vb;

            QDomElement cpOut = e.firstChildElement("connectionPointOut");
            m_outPort[lid] = cpRelScene(cpOut, p);
        }

        // ── outVariable (输出变量) ─────────────────────────────
        else if (tag == "outVariable") {
            const QString expr = e.firstChildElement("expression").text();

            auto* vb = new VarBoxItem(expr, VarBoxItem::OutVar);
            vb->setExplicitSize(xw, xh);
            vb->setData(0, lid);
            vb->setPos(p);
            addItem(vb);
            m_items[lid] = vb;
        }

        // ── inOutVariable (双向变量) ───────────────────────────
        else if (tag == "inOutVariable") {
            const QString expr = e.firstChildElement("expression").text();

            auto* vb = new VarBoxItem(expr, VarBoxItem::InOutVar);
            vb->setExplicitSize(xw, xh);
            vb->setData(0, lid);
            vb->setPos(p);
            addItem(vb);
            m_items[lid] = vb;

            QDomElement cpOut = e.firstChildElement("connectionPointOut");
            m_outPort[lid] = cpRelScene(cpOut, p);
        }

        // ── coil（线圈）───────────────────────────────────────
        else if (tag == "coil") {
            const QString varName = e.firstChildElement("variable").text();
            const QString storage = e.attribute("storage");
            CoilItem::CoilType ctype = CoilItem::Output;
            if (storage == "set")   ctype = CoilItem::SetCoil;
            if (storage == "reset") ctype = CoilItem::ResetCoil;

            auto* co = new CoilItem(ctype);
            co->setTagName(varName);
            co->setData(0, lid);
            co->setPos(p);
            addItem(co);
            m_items[lid] = co;

            QDomElement cpOut = e.firstChildElement("connectionPointOut");
            m_outPort[lid] = cpRelScene(cpOut, p);
        }

        // ── rightPowerRail（右母线）───────────────────────────
        else if (tag == "rightPowerRail") {
            auto* r = new QGraphicsRectItem(0, 0, xw, xh);
            r->setPen(QPen(QColor("#1565C0"), 2));
            r->setBrush(QBrush(QColor("#1565C0")));
            r->setFlags(QGraphicsItem::ItemIsSelectable |
                        QGraphicsItem::ItemIsMovable     |
                        QGraphicsItem::ItemSendsGeometryChanges);
            r->setData(0, lid);
            r->setPos(p);
            addItem(r);
            m_items[lid] = r;
        }

        // ── leftPowerRail（左母线）────────────────────────────
        else if (tag == "leftPowerRail") {
            auto* r = new QGraphicsRectItem(0, 0, xw, xh);
            r->setPen(QPen(QColor("#1565C0"), 2));
            r->setBrush(QBrush(QColor("#1565C0")));
            r->setFlags(QGraphicsItem::ItemIsSelectable |
                        QGraphicsItem::ItemIsMovable     |
                        QGraphicsItem::ItemSendsGeometryChanges);
            r->setData(0, lid);
            r->setPos(p);
            addItem(r);
            m_items[lid] = r;

            QDomElement cpOut = e.firstChildElement("connectionPointOut");
            m_outPort[lid] = cpRelScene(cpOut, p);
        }

        // ── comment（注释框）──────────────────────────────────
        else if (tag == "comment") {
            QDomElement content = e.firstChildElement("content");
            QString txt;
            for (QDomElement ch = content.firstChildElement();
                 !ch.isNull(); ch = ch.nextSiblingElement()) {
                txt = ch.text().trimmed(); break;
            }
            auto* r = new QGraphicsRectItem(p.x(), p.y(), xw, xh);
            r->setPen(QPen(QColor("#BBBBBB"), 1, Qt::DashLine));
            r->setBrush(QBrush(QColor("#FFFDE7")));
            addItem(r);

            auto* t = new QGraphicsTextItem();
            QFont f("Arial");
            f.setPixelSize(qMax(18, (int)(xh * 0.05)));
            t->setFont(f);
            t->setTextWidth(xw - 8);
            t->setPlainText(txt);
            t->setDefaultTextColor(QColor("#555"));
            t->setPos(p.x() + 4, p.y() + 4);
            addItem(t);
        }
    }
}

void PlcOpenViewer::drawFbdWires(const QDomElement& body)
{
    auto routeHvh = [](QPointF s, QPointF d) -> QPainterPath {
        QPainterPath path;
        path.moveTo(s);
        qreal midX = (s.x() + d.x()) / 2.0;
        path.lineTo(midX, s.y());
        path.lineTo(midX, d.y());
        path.lineTo(d);
        return path;
    };

    for (QDomElement e = body.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement())
    {
        const QString tag = e.tagName();
        int lid = e.attribute("localId","-1").toInt();

        auto processConn = [&](const QDomElement& cpIn, const QString& dstFp) {
            QDomNodeList conns = cpIn.elementsByTagName("connection");
            for (int k = 0; k < conns.count(); ++k) {
                QDomElement conn = conns.at(k).toElement();
                int     refId = conn.attribute("refLocalId","-1").toInt();
                QString fp    = conn.attribute("formalParameter","");

                // WireItem 提供宽点击区域（shape()）和选中高亮（paint()）
                auto* wire = new WireItem(QPointF(), QPointF());
                wire->setZValue(-1);
                addItem(wire);

                // 优先使用 XML 存储的折点路径（<position> 顺序 dst→src，绘制时反转）
                QDomNodeList posList = conn.elementsByTagName("position");
                if (posList.count() >= 2) {
                    QPainterPath path;
                    for (int j = posList.count() - 1; j >= 0; --j) {
                        QDomElement pos = posList.at(j).toElement();
                        QPointF pt(pos.attribute("x","0").toDouble() * kScale,
                                   pos.attribute("y","0").toDouble() * kScale);
                        if (j == posList.count() - 1)
                            path.moveTo(pt);
                        else
                            path.lineTo(pt);
                    }
                    wire->setPath(path);
                } else {
                    QPointF src = getOutputPortScene(refId, fp);
                    QPointF dst = getInputPortScene(lid, dstFp);
                    if (src.x() > -1e8 && dst.x() > -1e8)
                        wire->setPath(routeHvh(src, dst));
                }

                m_connections << FbdConn{refId, fp, lid, dstFp, wire};
            }
        };

        if (tag == "block") {
            QDomElement inVars = e.firstChildElement("inputVariables");
            QDomNodeList inList = inVars.elementsByTagName("variable");
            for (int k = 0; k < inList.count(); ++k) {
                QDomElement iv = inList.at(k).toElement();
                processConn(iv.firstChildElement("connectionPointIn"),
                            iv.attribute("formalParameter"));
            }
        }
        else if (tag == "outVariable" || tag == "inOutVariable" || tag == "contact") {
            processConn(e.firstChildElement("connectionPointIn"), "");
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// SFC 渲染
// ═══════════════════════════════════════════════════════════════
void PlcOpenViewer::buildSfc(const QDomElement& body)
{
    createSfcItems(body);
    drawSfcWires(body);
}

QGraphicsTextItem* PlcOpenViewer::addLabel(const QString& text,
                                            const QRectF& rect,
                                            Qt::AlignmentFlag,
                                            int /*fontSize*/)
{
    auto* item = new QGraphicsTextItem(text);
    QFont f("Arial");
    f.setPixelSize(qMax(10, (int)(rect.height() * 0.18)));
    item->setFont(f);
    item->setDefaultTextColor(Qt::black);
    item->setPos(rect.x() + (rect.width()  - item->boundingRect().width())  / 2.0,
                 rect.y() + (rect.height() - item->boundingRect().height()) / 2.0);
    addItem(item);
    return item;
}

void PlcOpenViewer::createSfcItems(const QDomElement& body)
{
    QPen stepPen(kColStep, 1.5);
    QPen transPen(kColTrans, 2.0);
    QPen divPen(kColTrans, 1.5);

    for (QDomElement e = body.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement())
    {
        const QString tag = e.tagName();
        int lid = e.attribute("localId","-1").toInt();
        QPointF p = absPos(e);
        double w  = e.attribute("width","80").toDouble() * kScale;
        double h  = e.attribute("height","30").toDouble() * kScale;
        QRectF rect(p.x(), p.y(), w, h);

        if (tag == "step") {
            QString name = e.attribute("name");
            bool initial = (e.attribute("initialStep","false") == "true");
            auto* r = new QGraphicsRectItem(rect);
            r->setPen(stepPen);
            r->setBrush(QBrush(kColStepFill));
            r->setFlag(QGraphicsItem::ItemIsSelectable, true);
            r->setFlag(QGraphicsItem::ItemIsMovable, true);
            addItem(r);
            if (initial) {
                QRectF inner = rect.adjusted(3, 3, -3, -3);
                auto* ri = new QGraphicsRectItem(inner);
                ri->setPen(stepPen); ri->setBrush(Qt::NoBrush); addItem(ri);
            }
            addLabel(name, rect);
            m_outPort[lid] = QPointF(p.x() + w/2, p.y() + h);
        }
        else if (tag == "transition") {
            double cx = p.x() + w / 2.0;
            auto* line = new QGraphicsLineItem(cx - 15*kScale, p.y(),
                                               cx + 15*kScale, p.y());
            line->setPen(transPen); addItem(line);

            QDomElement condElem = e.firstChildElement("condition");
            QString condText;
            QDomElement inlineEl = condElem.firstChildElement("inline");
            if (!inlineEl.isNull()) {
                QDomNodeList ps = inlineEl.firstChildElement("ST").childNodes();
                for (int k = 0; k < ps.count(); ++k) {
                    QDomElement pe = ps.at(k).toElement();
                    if (!pe.isNull()) { condText = pe.text().trimmed(); break; }
                }
            }
            if (condText.isEmpty()) {
                QDomElement refEl = condElem.firstChildElement("reference");
                if (!refEl.isNull())
                    condText = refEl.attribute("name");
            }
            if (!condText.isEmpty()) {
                auto* t = new QGraphicsTextItem(condText);
                QFont f("Arial");
                f.setPixelSize(18);
                t->setFont(f);
                t->setPos(cx + 18*kScale/2, p.y() - 12);
                addItem(t);
            }

            m_outPort[lid] = QPointF(p.x() + w/2, p.y() + 2);
        }
        else if (tag == "selectionDivergence") {
            auto* line = new QGraphicsLineItem(p.x(), p.y(), p.x()+w, p.y());
            line->setPen(divPen); addItem(line);
            QDomNodeList outPorts = e.elementsByTagName("connectionPointOut");
            for (int k = 0; k < outPorts.count(); ++k) {
                QDomElement cpO = outPorts.at(k).toElement();
                QDomElement rel = cpO.firstChildElement("relPosition");
                double ox = rel.attribute("x","0").toDouble() * kScale;
                double oy = rel.attribute("y","0").toDouble() * kScale;
                m_namedOutPort[lid][QString::number(k)] =
                    QPointF(p.x()+ox, p.y()+oy);
            }
            m_outPort[lid] = m_namedOutPort[lid].value("0");
        }
        else if (tag == "selectionConvergence") {
            auto* line = new QGraphicsLineItem(p.x(), p.y(), p.x()+w, p.y());
            line->setPen(divPen); addItem(line);
            QDomElement cpOut = e.firstChildElement("connectionPointOut")
                                   .firstChildElement("relPosition");
            double ox = cpOut.attribute("x","0").toDouble() * kScale;
            double oy = cpOut.attribute("y","0").toDouble() * kScale;
            m_outPort[lid] = QPointF(p.x()+ox, p.y()+oy);
        }
        else if (tag == "jumpStep") {
            QString target = e.attribute("targetName");
            QPolygonF tri;
            tri << QPointF(p.x()+w/2-6, p.y())
                << QPointF(p.x()+w/2+6, p.y())
                << QPointF(p.x()+w/2,   p.y()+h);
            auto* poly = new QGraphicsPolygonItem(tri);
            poly->setPen(QPen(kColStep,1.5));
            poly->setBrush(QBrush(kColStepFill));
            poly->setFlag(QGraphicsItem::ItemIsSelectable, true);
            poly->setFlag(QGraphicsItem::ItemIsMovable, true);
            addItem(poly);
            auto* t = new QGraphicsTextItem(target);
            QFont f("Arial");
            f.setPixelSize(18);
            t->setFont(f);
            t->setPos(p.x()+w/2+10, p.y()); addItem(t);
        }
        else if (tag == "actionBlock") {
            QStringList lines;
            QDomNodeList actions = e.elementsByTagName("action");
            for (int k = 0; k < actions.count(); ++k) {
                QDomElement ac = actions.at(k).toElement();
                QString qualifier = ac.attribute("qualifier");
                QString duration  = ac.attribute("duration");

                QString actionName;
                QDomElement refEl = ac.firstChildElement("reference");
                if (!refEl.isNull()) {
                    actionName = refEl.attribute("name");
                } else {
                    QDomElement inl = ac.firstChildElement("inline");
                    QDomNodeList ps = inl.firstChildElement("ST").childNodes();
                    for (int m2 = 0; m2 < ps.count(); ++m2) {
                        QDomElement pe = ps.at(m2).toElement();
                        if (!pe.isNull()) { actionName = pe.text().trimmed(); break; }
                    }
                }

                QString line = qualifier.leftJustified(2);
                line += " | " + actionName;
                if (!duration.isEmpty()) line += "  [" + duration + "]";
                lines << line;
            }

            auto* r = new QGraphicsRectItem(rect);
            r->setPen(QPen(kColBlock, 1.2));
            r->setBrush(QBrush(QColor("#FFF9C4")));
            r->setFlag(QGraphicsItem::ItemIsSelectable, true);
            addItem(r);
            auto* t = new QGraphicsTextItem();
            QFont f("Courier New");
            f.setPixelSize(qMax(14, (int)(rect.height() * 0.05)));
            t->setFont(f);
            t->setTextWidth(rect.width() - 4);
            t->setPlainText(lines.join("\n"));
            t->setPos(rect.x() + 2, rect.y() + 2);
            addItem(t);
        }
    }
}

void PlcOpenViewer::drawSfcWires(const QDomElement& body)
{
    QPen wirePen(kColTrans, 1.5);

    for (QDomElement e = body.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement())
    {
        for (QDomElement cpIn = e.firstChildElement("connectionPointIn");
             !cpIn.isNull(); cpIn = cpIn.nextSiblingElement("connectionPointIn"))
        {
            QDomElement relPos = cpIn.firstChildElement("relPosition");
            QPointF ePos = absPos(e);
            QPointF dst(ePos.x() + relPos.attribute("x","0").toDouble()*kScale,
                        ePos.y() + relPos.attribute("y","0").toDouble()*kScale);

            QDomNodeList conns = cpIn.elementsByTagName("connection");
            for (int k = 0; k < conns.count(); ++k) {
                QDomElement conn = conns.at(k).toElement();
                int refId = conn.attribute("refLocalId","-1").toInt();

                auto* pi = new QGraphicsPathItem();
                QPainterPath path;

                QDomNodeList posList = conn.elementsByTagName("position");
                if (posList.count() >= 2) {
                    for (int j = posList.count() - 1; j >= 0; --j) {
                        QDomElement pos = posList.at(j).toElement();
                        QPointF pt(pos.attribute("x","0").toDouble() * kScale,
                                   pos.attribute("y","0").toDouble() * kScale);
                        if (j == posList.count() - 1)
                            path.moveTo(pt);
                        else
                            path.lineTo(pt);
                    }
                } else {
                    QPointF src = m_outPort.value(refId, QPointF(-1e9,-1e9));
                    if (src.x() < -1e8) continue;
                    path.moveTo(src);
                    path.lineTo(dst);
                }

                pi->setPath(path);
                pi->setPen(wirePen);
                addItem(pi);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// 动态导线路由 & 序列化
// ═══════════════════════════════════════════════════════════════

QPointF PlcOpenViewer::getOutputPortScene(int lid, const QString& param) const
{
    QGraphicsItem* gi = m_items.value(lid);
    if (!gi) return {-1e9, -1e9};

    if (auto* fb = qgraphicsitem_cast<FunctionBlockItem*>(gi)) {
        int idx = param.isEmpty() ? 0 : fb->outputPortIndex(param);
        if (idx < 0) idx = 0;
        return (idx < fb->outputCount()) ? fb->outputPortPos(idx) : QPointF(-1e9, -1e9);
    }
    if (auto* vb = qgraphicsitem_cast<VarBoxItem*>(gi))   return vb->rightPort();
    if (auto* ct = qgraphicsitem_cast<ContactItem*>(gi))  return ct->rightPort();

    QRectF r = gi->sceneBoundingRect();
    return { r.right(), r.center().y() };
}

QPointF PlcOpenViewer::getInputPortScene(int lid, const QString& param) const
{
    QGraphicsItem* gi = m_items.value(lid);
    if (!gi) return {-1e9, -1e9};

    if (auto* fb = qgraphicsitem_cast<FunctionBlockItem*>(gi)) {
        int idx = param.isEmpty() ? 0 : fb->inputPortIndex(param);
        if (idx < 0) idx = 0;
        return (idx < fb->inputCount()) ? fb->inputPortPos(idx) : QPointF(-1e9, -1e9);
    }
    if (auto* vb = qgraphicsitem_cast<VarBoxItem*>(gi))  return vb->leftPort();
    if (auto* ct = qgraphicsitem_cast<ContactItem*>(gi)) return ct->leftPort();

    return {-1e9, -1e9};
}

// ── 通用路径构建：H-V-H，支持 src/dst 侧垂直折点 ─────────────
static QPainterPath buildWirePath(const QPointF& src, const QPointF& dst,
                                   qreal midX, qreal srcJogY, qreal dstJogY)
{
    const qreal sY = qIsNaN(srcJogY) ? src.y() : srcJogY;
    const qreal dY = qIsNaN(dstJogY) ? dst.y() : dstJogY;
    QPainterPath path;
    path.moveTo(src);
    if (!qIsNaN(srcJogY))
        path.lineTo(src.x(), sY);   // src 侧垂直折
    path.lineTo(midX, sY);          // 上段水平
    path.lineTo(midX, dY);          // 中间垂直
    if (!qIsNaN(dstJogY))
        path.lineTo(dst.x(), dY);   // 下段水平
    path.lineTo(dst);               // dst 侧垂直折（若 dstJogY 有效）
    return path;
}

void PlcOpenViewer::updateAllWires()
{
    if (m_updatingWires) return;
    m_updatingWires = true;

    for (FbdConn& c : m_connections) {
        if (!c.wire || c.wire->scene() != this) continue;
        QPointF src = getOutputPortScene(c.srcId, c.srcParam);
        QPointF dst = getInputPortScene (c.dstId, c.dstParam);
        if (src.x() < -1e8 || dst.x() < -1e8) {
            c.wire->setPath(QPainterPath());
            continue;
        }
        qreal midX = qIsNaN(c.customMidX) ? (src.x() + dst.x()) / 2.0 : c.customMidX;
        c.wire->setPath(buildWirePath(src, dst, midX, c.srcJogY, c.dstJogY));
    }

    m_updatingWires = false;
}

// ═══════════════════════════════════════════════════════════════
// 端口反向查找 & 线段几何判断
// ═══════════════════════════════════════════════════════════════

PlcOpenViewer::PortRef PlcOpenViewer::findPortAt(const QPointF& pos, qreal radius) const
{
    PortRef best;
    qreal bestD2 = radius * radius;

    auto check = [&](int lid, const QString& param, bool isOut, const QPointF& pt) {
        qreal dx = pt.x() - pos.x(), dy = pt.y() - pos.y();
        qreal d2 = dx*dx + dy*dy;
        if (d2 < bestD2) { bestD2 = d2; best = {lid, param, isOut}; }
    };

    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        int lid = it.key();
        QGraphicsItem* gi = it.value();
        if (auto* fb = qgraphicsitem_cast<FunctionBlockItem*>(gi)) {
            for (int i = 0; i < fb->inputCount(); ++i)
                check(lid, fb->inputPortName(i),  false, fb->inputPortPos(i));
            for (int i = 0; i < fb->outputCount(); ++i)
                check(lid, fb->outputPortName(i), true,  fb->outputPortPos(i));
        } else if (auto* vb = qgraphicsitem_cast<VarBoxItem*>(gi)) {
            if (vb->role() != VarBoxItem::OutVar)
                check(lid, {}, true,  vb->rightPort());
            if (vb->role() != VarBoxItem::InVar)
                check(lid, {}, false, vb->leftPort());
        } else if (auto* ct = qgraphicsitem_cast<ContactItem*>(gi)) {
            check(lid, {}, false, ct->leftPort());
            check(lid, {}, true,  ct->rightPort());
        } else if (auto* coil = qgraphicsitem_cast<CoilItem*>(gi)) {
            check(lid, {}, false, coil->leftPort());
            check(lid, {}, true,  coil->rightPort());
        }
    }
    return best;
}

bool PlcOpenViewer::nearWireVertSeg(const FbdConn& c, const QPointF& pos, qreal tol) const
{
    if (!c.wire) return false;
    QPainterPath p = c.wire->path();
    if (p.elementCount() < 4) return false;
    // 在路径中找垂直线段（dx ≈ 0, 有一定长度），检测 pos 是否在其附近
    int n = p.elementCount();
    for (int i = 0; i < n-1; ++i) {
        qreal dx = p.elementAt(i+1).x - p.elementAt(i).x;
        qreal y0 = qMin(p.elementAt(i).y, p.elementAt(i+1).y);
        qreal y1 = qMax(p.elementAt(i).y, p.elementAt(i+1).y);
        if (qAbs(dx) < 1.0 && (y1 - y0) > 4.0) {   // 垂直线段
            qreal mx = p.elementAt(i).x;
            if (qAbs(pos.x() - mx) <= tol && pos.y() >= y0 - tol && pos.y() <= y1 + tol)
                return true;
        }
    }
    return false;
}

int PlcOpenViewer::nearWireHorizSeg(const FbdConn& c, const QPointF& pos, qreal tol) const
{
    if (!c.wire) return 0;
    QPainterPath p = c.wire->path();
    int n = p.elementCount();
    // 收集所有水平线段（dy ≈ 0）
    QVector<int> hSegs;
    for (int i = 0; i < n-1; ++i) {
        qreal dy = qAbs(p.elementAt(i+1).y - p.elementAt(i).y);
        if (dy < 1.0)
            hSegs << i;
    }
    // 对每段判断 pos 是否在上面
    for (int k = 0; k < hSegs.size(); ++k) {
        int  i  = hSegs[k];
        qreal y  = p.elementAt(i).y;
        qreal x0 = qMin(p.elementAt(i).x, p.elementAt(i+1).x);
        qreal x1 = qMax(p.elementAt(i).x, p.elementAt(i+1).x);
        if ((x1 - x0) < 2.0) continue;          // 退化线段，跳过
        if (qAbs(pos.y() - y) <= tol && pos.x() >= x0-tol && pos.x() <= x1+tol) {
            // 第一条 = src 侧，最后一条 = dst 侧
            return (k == 0) ? 1 : 2;
        }
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════
// 鼠标事件覆盖：导线确认绑定 + 线段拖拽 + 端点重连
// ═══════════════════════════════════════════════════════════════

void PlcOpenViewer::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    const QPointF pos = event->scenePos();

    // ── Mode_AddWire: 全部拦截（多折点画线）──────────────────────
    if (m_mode == Mode_AddWire) {
        auto snapG = [](qreal v){ return qRound(v / GridSize) * (qreal)GridSize; };

        if (event->button() == Qt::RightButton) {
            // 右键：≥2 个折点则确认，否则取消
            if (m_wirePoints.size() >= 2) {
                // 构建最终路径
                QPainterPath path;
                path.moveTo(m_wirePoints[0]);
                for (int i = 1; i < m_wirePoints.size(); ++i)
                    path.lineTo(m_wirePoints[i]);

                WireItem* wire = m_tempWire;
                m_tempWire = nullptr;
                removeItem(wire);   // AddWireCmd/AddItemCmd 的 redo 会重新 addItem
                wire->setPath(path);

                PortRef srcPort = findPortAt(m_wirePoints.first(), 20.0);
                PortRef dstPort = findPortAt(m_wirePoints.last(),  20.0);

                if (m_wirePoints.size() == 2 && srcPort.valid() && dstPort.valid()) {
                    int     sid = srcPort.isOutput ? srcPort.lid : dstPort.lid;
                    QString sp  = srcPort.isOutput ? srcPort.param : dstPort.param;
                    int     did = srcPort.isOutput ? dstPort.lid : srcPort.lid;
                    QString dp  = srcPort.isOutput ? dstPort.param : srcPort.param;
                    FbdConn conn{ sid, sp, did, dp, nullptr, qQNaN() };
                    m_undoStack->push(new AddWireCmd(this, wire, conn));
                } else {
                    m_undoStack->push(new AddItemCmd(this, wire, "Add Wire"));
                }
            } else {
                // 点数不足：取消
                if (m_tempWire) {
                    removeItem(m_tempWire);
                    delete m_tempWire;
                    m_tempWire = nullptr;
                }
            }
            m_wirePoints.clear();
            m_showPortSnap = false;
            update();
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton) {
            QPointF snap = snapToNearestPort(pos, 20.0);
            if (snap == pos)
                snap = QPointF(snapG(pos.x()), snapG(pos.y()));

            if (m_wirePoints.isEmpty()) {
                // 第一个折点：创建预览导线
                m_wirePoints << snap;
                m_tempWire = new WireItem(snap, snap);
                addItem(m_tempWire);
            } else {
                // 追加折点，更新预览路径
                m_wirePoints << snap;
                QPainterPath path;
                path.moveTo(m_wirePoints[0]);
                for (int i = 1; i < m_wirePoints.size(); ++i)
                    path.lineTo(m_wirePoints[i]);
                if (m_tempWire) m_tempWire->setPath(path);
            }
            m_showPortSnap = snapToNearestPort(pos, 20.0) != pos;
            m_portSnapPos  = snap;
            event->accept();
            return;
        }

        // 其他按键在画线模式下忽略
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        LadderScene::mousePressEvent(event);
        return;
    }

    // ── 2. Select 模式：检测端点拖拽 & 线段拖拽 ─────────────────
    if (m_mode == Mode_Select) {
        // 端点优先：8 px 以内
        for (int i = 0; i < m_connections.size(); ++i) {
            const FbdConn& c = m_connections[i];
            if (!c.wire || c.wire->scene() != this) continue;
            QPointF srcPt = getOutputPortScene(c.srcId, c.srcParam);
            QPointF dstPt = getInputPortScene (c.dstId, c.dstParam);
            auto dist = [&](const QPointF& a){ return QLineF(pos, a).length(); };
            if (dist(srcPt) < 8.0) {
                m_epDragIdx     = i;
                m_epDragIsSrc   = true;
                m_epDragOldConn = c;
                event->accept();
                return;
            }
            if (dist(dstPt) < 8.0) {
                m_epDragIdx     = i;
                m_epDragIsSrc   = false;
                m_epDragOldConn = c;
                event->accept();
                return;
            }
        }
        // 垂直线段拖拽（左右）
        for (int i = 0; i < m_connections.size(); ++i) {
            if (!m_connections[i].wire) continue;
            if (nearWireVertSeg(m_connections[i], pos)) {
                m_segDragIdx     = i;
                const QPainterPath& p = m_connections[i].wire->path();
                // 找路径中第一条垂直段的 x 作为 oldMidX
                int pn = p.elementCount();
                qreal oldMx = 0.0;
                for (int k = 0; k < pn-1; ++k) {
                    if (qAbs(p.elementAt(k+1).x - p.elementAt(k).x) < 1.0) {
                        oldMx = p.elementAt(k).x; break;
                    }
                }
                m_segDragOldMidX = oldMx;
                event->accept();
                return;
            }
        }
        // 水平线段拖拽（上下）
        for (int i = 0; i < m_connections.size(); ++i) {
            if (!m_connections[i].wire) continue;
            int side = nearWireHorizSeg(m_connections[i], pos);
            if (side != 0) {
                m_horizDragIdx   = i;
                m_horizDragIsSrc = (side == 1);
                // 当前 jog 值（NaN 表示尚未设置，此时取端口高度）
                const FbdConn& c = m_connections[i];
                if (m_horizDragIsSrc) {
                    m_horizDragOldY = qIsNaN(c.srcJogY)
                        ? getOutputPortScene(c.srcId, c.srcParam).y()
                        : c.srcJogY;
                } else {
                    m_horizDragOldY = qIsNaN(c.dstJogY)
                        ? getInputPortScene(c.dstId, c.dstParam).y()
                        : c.dstJogY;
                }
                event->accept();
                return;
            }
        }
    }

    LadderScene::mousePressEvent(event);
}

void PlcOpenViewer::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    const QPointF pos = event->scenePos();

    // ── 端点拖拽 ────────────────────────────────────────────────
    if (m_epDragIdx >= 0) {
        const QPointF snap = snapToNearestPort(pos, 20.0);
        m_portSnapPos  = snap;
        m_showPortSnap = (snap != pos);
        FbdConn& c = m_connections[m_epDragIdx];
        QPainterPath path;
        if (m_epDragIsSrc) {
            QPointF dst = getInputPortScene(c.dstId, c.dstParam);
            qreal midX  = (snap.x() + dst.x()) / 2.0;
            path.moveTo(snap);
            path.lineTo(midX, snap.y());
            path.lineTo(midX, dst.y());
            path.lineTo(dst);
        } else {
            QPointF src = getOutputPortScene(c.srcId, c.srcParam);
            qreal midX  = (src.x() + snap.x()) / 2.0;
            path.moveTo(src);
            path.lineTo(midX, src.y());
            path.lineTo(midX, snap.y());
            path.lineTo(snap);
        }
        if (c.wire) c.wire->setPath(path);
        update();
        return;
    }

    // ── 垂直线段拖拽（左右） ─────────────────────────────────────
    if (m_segDragIdx >= 0) {
        qreal newMidX = qRound(pos.x() / GridSize) * (qreal)GridSize;
        FbdConn& c = m_connections[m_segDragIdx];
        c.customMidX = newMidX;
        QPointF src = getOutputPortScene(c.srcId, c.srcParam);
        QPointF dst = getInputPortScene (c.dstId, c.dstParam);
        if (src.x() > -1e8 && dst.x() > -1e8)
            c.wire->setPath(buildWirePath(src, dst, newMidX, c.srcJogY, c.dstJogY));
        for (auto* v : views()) v->setCursor(Qt::SizeHorCursor);
        update();
        return;
    }

    // ── 水平线段拖拽（上下） ─────────────────────────────────────
    if (m_horizDragIdx >= 0) {
        qreal newY = qRound(pos.y() / GridSize) * (qreal)GridSize;
        FbdConn& c = m_connections[m_horizDragIdx];
        if (m_horizDragIsSrc) c.srcJogY = newY; else c.dstJogY = newY;
        QPointF src = getOutputPortScene(c.srcId, c.srcParam);
        QPointF dst = getInputPortScene (c.dstId, c.dstParam);
        qreal midX  = qIsNaN(c.customMidX) ? (src.x() + dst.x()) / 2.0 : c.customMidX;
        if (src.x() > -1e8 && dst.x() > -1e8)
            c.wire->setPath(buildWirePath(src, dst, midX, c.srcJogY, c.dstJogY));
        for (auto* v : views()) v->setCursor(Qt::SizeVerCursor);
        update();
        return;
    }

    // ── 悬停反馈：改变光标 ──────────────────────────────────────
    if (m_mode == Mode_Select) {
        Qt::CursorShape cur = Qt::ArrowCursor;
        for (const FbdConn& c : m_connections) {
            if (!c.wire || c.wire->scene() != this) continue;
            if (nearWireVertSeg(c, pos))         { cur = Qt::SizeHorCursor; break; }
            if (nearWireHorizSeg(c, pos) != 0)   { cur = Qt::SizeVerCursor; break; }
            QPointF srcPt = getOutputPortScene(c.srcId, c.srcParam);
            QPointF dstPt = getInputPortScene (c.dstId, c.dstParam);
            if (QLineF(pos, srcPt).length() < 8.0 ||
                QLineF(pos, dstPt).length() < 8.0)
            { cur = Qt::CrossCursor; break; }
        }
        for (auto* v : views()) v->setCursor(cur);
    }

    // ── 折线画线预览：有积累折点时更新预览路径 ──────────────────
    if (m_mode == Mode_AddWire && !m_wirePoints.isEmpty() && m_tempWire) {
        QPointF snap = snapToNearestPort(pos, 20.0);
        if (snap == pos) {
            auto snapG = [](qreal v){ return qRound(v / GridSize) * (qreal)GridSize; };
            snap = QPointF(snapG(pos.x()), snapG(pos.y()));
        }
        m_showPortSnap = (snap != pos);
        m_portSnapPos  = snap;
        QPainterPath p;
        p.moveTo(m_wirePoints[0]);
        for (int i = 1; i < m_wirePoints.size(); ++i)
            p.lineTo(m_wirePoints[i]);
        p.lineTo(snap);
        m_tempWire->setPath(p);
        update();
        return;  // 跳过 LadderScene（防止 setEndPos 覆盖自定义路径）
    }

    LadderScene::mouseMoveEvent(event);
}

void PlcOpenViewer::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        LadderScene::mouseReleaseEvent(event);
        return;
    }

    // ── 端点拖拽结束 ────────────────────────────────────────────
    if (m_epDragIdx >= 0) {
        const QPointF snap = snapToNearestPort(event->scenePos(), 20.0);
        PortRef newPort = findPortAt(snap, 20.0);

        FbdConn& c = m_connections[m_epDragIdx];
        FbdConn newConn = m_epDragOldConn;
        newConn.wire    = c.wire;
        bool valid = false;

        if (newPort.valid()) {
            if (m_epDragIsSrc && newPort.isOutput
                && newPort.lid != m_epDragOldConn.dstId) {
                newConn.srcId   = newPort.lid;
                newConn.srcParam = newPort.param;
                valid = true;
            } else if (!m_epDragIsSrc && !newPort.isOutput
                       && newPort.lid != m_epDragOldConn.srcId) {
                newConn.dstId   = newPort.lid;
                newConn.dstParam = newPort.param;
                valid = true;
            }
        }

        if (valid) {
            FbdConn oldConn = m_epDragOldConn;
            oldConn.wire    = c.wire;
            m_undoStack->push(new MoveWireEndpointCmd(this, c.wire, oldConn, newConn));
        } else {
            // 恢复原连接
            c = m_epDragOldConn;
            updateAllWires();
        }
        m_epDragIdx    = -1;
        m_showPortSnap = false;
        for (auto* v : views()) v->setCursor(Qt::ArrowCursor);
        update();
        return;
    }

    // ── 垂直线段拖拽结束 ─────────────────────────────────────────
    if (m_segDragIdx >= 0) {
        qreal newMidX = m_connections[m_segDragIdx].customMidX;
        if (!qIsNaN(newMidX) && qAbs(newMidX - m_segDragOldMidX) > 1.0) {
            m_undoStack->push(new MoveWireSegmentCmd(
                this, m_connections[m_segDragIdx].wire,
                m_segDragOldMidX, newMidX));
        } else {
            m_connections[m_segDragIdx].customMidX = qQNaN();
            updateAllWires();
        }
        m_segDragIdx = -1;
        for (auto* v : views()) v->setCursor(Qt::ArrowCursor);
        return;
    }

    // ── 水平线段拖拽结束 ─────────────────────────────────────────
    if (m_horizDragIdx >= 0) {
        FbdConn& c = m_connections[m_horizDragIdx];
        qreal newY = m_horizDragIsSrc ? c.srcJogY : c.dstJogY;
        if (!qIsNaN(newY) && qAbs(newY - m_horizDragOldY) > 1.0) {
            m_undoStack->push(new MoveWireHorizCmd(
                this, c.wire, m_horizDragIsSrc, m_horizDragOldY, newY));
        } else {
            // 未移动 → 恢复原值
            if (m_horizDragIsSrc) c.srcJogY = qIsNaN(m_horizDragOldY) ? qQNaN() : m_horizDragOldY;
            else                  c.dstJogY = qIsNaN(m_horizDragOldY) ? qQNaN() : m_horizDragOldY;
            updateAllWires();
        }
        m_horizDragIdx = -1;
        for (auto* v : views()) v->setCursor(Qt::ArrowCursor);
        return;
    }

    LadderScene::mouseReleaseEvent(event);
}

// keyPressEvent: Escape 清除折线画线状态，其余交给 LadderScene
void PlcOpenViewer::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && m_mode == Mode_AddWire) {
        m_wirePoints.clear();
        // LadderScene::keyPressEvent 会清理 m_tempWire 并切换到 Mode_Select
    }
    LadderScene::keyPressEvent(event);
}

// drawForeground: 在继承的端口吸附指示器基础上，画出导线端点小圆圈
void PlcOpenViewer::drawForeground(QPainter* painter, const QRectF& rect)
{
    LadderScene::drawForeground(painter, rect);   // 端口吸附绿圈

    if (m_mode != Mode_Select) return;
    // 在每条连接导线的两个端点画蓝色小圆，提示可拖拽
    painter->setPen(QPen(QColor("#0078D7"), 1.2));
    painter->setBrush(QColor("#FFFFFF"));
    const qreal r = 3.5;
    for (const FbdConn& c : m_connections) {
        if (!c.wire || c.wire->scene() != this) continue;
        QPointF srcPt = getOutputPortScene(c.srcId, c.srcParam);
        QPointF dstPt = getInputPortScene (c.dstId, c.dstParam);
        if (srcPt.x() > -1e8) painter->drawEllipse(srcPt, r, r);
        if (dstPt.x() > -1e8) painter->drawEllipse(dstPt, r, r);
    }
}

QDomElement PlcOpenViewer::findElemById(const QDomElement& root, int lid)
{
    for (QDomElement e = root.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement()) {
        if (e.attribute("localId", "-1").toInt() == lid) return e;
    }
    return {};
}

void PlcOpenViewer::syncPositionsToDoc()
{
    if (m_bodyDoc.isNull()) return;
    QDomElement root = m_bodyDoc.documentElement();

    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        QDomElement elem = findElemById(root, it.key());
        if (elem.isNull()) continue;

        QPointF p = it.value()->pos() / kScale;
        QDomElement posEl = elem.firstChildElement("position");
        if (!posEl.isNull()) {
            posEl.setAttribute("x", QString::number(qRound(p.x())));
            posEl.setAttribute("y", QString::number(qRound(p.y())));
        }
    }
}

void PlcOpenViewer::syncWirePathsToDoc()
{
    if (m_bodyDoc.isNull()) return;
    QDomElement root = m_bodyDoc.documentElement();

    for (const FbdConn& c : m_connections) {
        if (!c.wire) continue;
        QPainterPath path = c.wire->path();
        if (path.elementCount() < 2) continue;

        QDomElement dstElem = findElemById(root, c.dstId);
        if (dstElem.isNull()) continue;

        QDomElement connElem;
        if (dstElem.tagName() == "block") {
            QDomElement inVars = dstElem.firstChildElement("inputVariables");
            QDomNodeList vars = inVars.elementsByTagName("variable");
            for (int k = 0; k < vars.count() && connElem.isNull(); ++k) {
                QDomElement v = vars.at(k).toElement();
                if (v.attribute("formalParameter") != c.dstParam) continue;
                QDomElement cpIn = v.firstChildElement("connectionPointIn");
                QDomNodeList conns = cpIn.elementsByTagName("connection");
                for (int j = 0; j < conns.count(); ++j) {
                    QDomElement conn = conns.at(j).toElement();
                    if (conn.attribute("refLocalId", "-1").toInt() == c.srcId) {
                        connElem = conn; break;
                    }
                }
            }
        } else {
            QDomElement cpIn = dstElem.firstChildElement("connectionPointIn");
            QDomNodeList conns = cpIn.elementsByTagName("connection");
            for (int j = 0; j < conns.count(); ++j) {
                QDomElement conn = conns.at(j).toElement();
                if (conn.attribute("refLocalId", "-1").toInt() == c.srcId) {
                    connElem = conn; break;
                }
            }
        }
        if (connElem.isNull()) continue;

        QList<QDomNode> toRemove;
        QDomNodeList posElems = connElem.childNodes();
        for (int k = 0; k < posElems.count(); ++k) {
            QDomElement pe = posElems.at(k).toElement();
            if (!pe.isNull() && pe.tagName() == "position")
                toRemove << pe;
        }
        for (QDomNode n : toRemove)
            connElem.removeChild(n);

        int n = path.elementCount();
        for (int i = n - 1; i >= 0; --i) {
            QPainterPath::Element el = path.elementAt(i);
            QDomElement posEl = m_bodyDoc.createElement("position");
            posEl.setAttribute("x", QString::number(qRound(el.x / kScale)));
            posEl.setAttribute("y", QString::number(qRound(el.y / kScale)));
            connElem.appendChild(posEl);
        }
    }
}

QString PlcOpenViewer::toXmlString()
{
    if (m_bodyLanguage.isEmpty()) return {};

    if (m_isNewScene) {
        buildBodyFromScene();
    } else {
        syncPositionsToDoc();
        syncWirePathsToDoc();
    }
    return m_bodyLanguage + "\n" + m_bodyDoc.toString(2);
}

void PlcOpenViewer::buildBodyFromScene()
{
    m_bodyDoc = QDomDocument();
    QDomElement root = m_bodyDoc.createElement(m_bodyLanguage);
    m_bodyDoc.appendChild(root);

    auto addPos = [&](QDomElement& parent, QPointF scenePos) {
        QDomElement pos = m_bodyDoc.createElement("position");
        pos.setAttribute("x", QString::number(scenePos.x() / kScale, 'f', 0));
        pos.setAttribute("y", QString::number(scenePos.y() / kScale, 'f', 0));
        parent.appendChild(pos);
    };

    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        const int lid = it.key();
        QGraphicsItem* gi = it.value();

        if (auto* ct = qgraphicsitem_cast<ContactItem*>(gi)) {
            QDomElement e = m_bodyDoc.createElement("contact");
            e.setAttribute("localId", lid);
            bool neg = (ct->contactType() == ContactItem::NormalClosed);
            e.setAttribute("negated", neg ? "true" : "false");
            QString edge = "none";
            if (ct->contactType() == ContactItem::PositiveTransition) edge = "rising";
            if (ct->contactType() == ContactItem::NegativeTransition) edge = "falling";
            e.setAttribute("edge", edge);
            addPos(e, gi->pos());
            QDomElement var = m_bodyDoc.createElement("variable");
            var.appendChild(m_bodyDoc.createTextNode(ct->tagName()));
            e.appendChild(var);
            e.appendChild(m_bodyDoc.createElement("connectionPointIn"));
            e.appendChild(m_bodyDoc.createElement("connectionPointOut"));
            root.appendChild(e);

        } else if (auto* co = qgraphicsitem_cast<CoilItem*>(gi)) {
            QDomElement e = m_bodyDoc.createElement("coil");
            e.setAttribute("localId", lid);
            bool neg = (co->coilType() == CoilItem::Negated);
            e.setAttribute("negated", neg ? "true" : "false");
            QString storage = "none";
            if (co->coilType() == CoilItem::SetCoil)   storage = "set";
            if (co->coilType() == CoilItem::ResetCoil) storage = "reset";
            e.setAttribute("storage", storage);
            addPos(e, gi->pos());
            QDomElement var = m_bodyDoc.createElement("variable");
            var.appendChild(m_bodyDoc.createTextNode(co->tagName()));
            e.appendChild(var);
            e.appendChild(m_bodyDoc.createElement("connectionPointIn"));
            e.appendChild(m_bodyDoc.createElement("connectionPointOut"));
            root.appendChild(e);

        } else if (auto* fb = qgraphicsitem_cast<FunctionBlockItem*>(gi)) {
            QDomElement e = m_bodyDoc.createElement("block");
            e.setAttribute("localId", lid);
            e.setAttribute("typeName", fb->blockType());
            e.setAttribute("instanceName", fb->instanceName());
            addPos(e, gi->pos());
            root.appendChild(e);

        } else if (auto* vb = qgraphicsitem_cast<VarBoxItem*>(gi)) {
            QString tag = (vb->role() == VarBoxItem::InVar)    ? "inVariable"  :
                          (vb->role() == VarBoxItem::OutVar)   ? "outVariable" : "inOutVariable";
            QDomElement e = m_bodyDoc.createElement(tag);
            e.setAttribute("localId", lid);
            addPos(e, gi->pos());
            QDomElement expr = m_bodyDoc.createElement("expression");
            expr.appendChild(m_bodyDoc.createTextNode(vb->expression()));
            e.appendChild(expr);
            if (vb->role() != VarBoxItem::InVar)
                e.appendChild(m_bodyDoc.createElement("connectionPointIn"));
            if (vb->role() != VarBoxItem::OutVar)
                e.appendChild(m_bodyDoc.createElement("connectionPointOut"));
            root.appendChild(e);

        } else {
            // leftPowerRail（raw QGraphicsRectItem）
            QDomElement e = m_bodyDoc.createElement("leftPowerRail");
            e.setAttribute("localId", lid);
            QRectF br = gi->boundingRect();
            e.setAttribute("width",  QString::number(br.width()  / kScale, 'f', 0));
            e.setAttribute("height", QString::number(br.height() / kScale, 'f', 0));
            addPos(e, gi->pos());
            QDomElement cpOut = m_bodyDoc.createElement("connectionPointOut");
            cpOut.setAttribute("formalParameter", "");
            e.appendChild(cpOut);
            root.appendChild(e);
        }
    }

    // 处理用户画的导线：匹配端口坐标 → 写入目标元素的 <connectionPointIn>
    const qreal tol = 15.0;
    for (QGraphicsItem* gi : items()) {
        if (gi->type() != WireItem::Type) continue;
        auto* wire = static_cast<WireItem*>(gi);
        QPointF startPt = wire->startPos();
        QPointF endPt   = wire->endPos();

        int srcId = -1;
        for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
            QPointF rp = getOutputPortScene(it.key(), {});
            if (rp.x() < -1e8) continue;
            if ((rp - startPt).manhattanLength() < tol) { srcId = it.key(); break; }
        }

        int dstId = -1;
        for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
            QPointF lp = getInputPortScene(it.key(), {});
            if (lp.x() < -1e8) continue;
            if ((lp - endPt).manhattanLength() < tol) { dstId = it.key(); break; }
        }

        if (srcId < 0 || dstId < 0) continue;

        QDomElement dstElem = findElemById(root, dstId);
        if (dstElem.isNull()) continue;

        QDomElement cpIn = dstElem.firstChildElement("connectionPointIn");
        if (cpIn.isNull()) continue;

        QDomElement conn = m_bodyDoc.createElement("connection");
        conn.setAttribute("refLocalId", srcId);
        conn.setAttribute("formalParameter", "");
        cpIn.appendChild(conn);
    }
}

// ── 空白画布初始化 ─────────────────────────────────────────────
void PlcOpenViewer::initEmpty(const QString& lang)
{
    m_connections.clear();
    clear();
    m_undoStack->clear();
    m_outPort.clear();
    m_namedOutPort.clear();
    m_items.clear();

    m_bodyLanguage = lang.toUpper().isEmpty() ? "LD" : lang.toUpper();
    m_bodyDoc      = QDomDocument();
    m_bodyDoc.appendChild(m_bodyDoc.createElement(m_bodyLanguage));
    m_isNewScene   = true;

    // 左电源母线（放在左上角，用户可自由移动）
    auto* rail = new QGraphicsRectItem(0, 0, 8, 240);
    rail->setPen(QPen(QColor("#1565C0"), 2));
    rail->setBrush(QBrush(QColor("#1565C0")));
    rail->setFlag(QGraphicsItem::ItemIsSelectable, true);
    rail->setFlag(QGraphicsItem::ItemIsMovable, true);
    rail->setPos(40, 40);
    int lid = m_nextLocalId++;
    rail->setData(0, lid);
    m_items[lid] = rail;
    addItem(rail);
}
