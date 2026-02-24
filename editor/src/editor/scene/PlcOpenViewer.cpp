// PlcOpenViewer.cpp — 统一图形编辑/查看场景
// 支持 PLCopen XML 导入 (LD/FBD/SFC) 以及空白可编辑画布

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
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QInputDialog>
#include <QDomDocument>
#include <QFont>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <algorithm>

// ─────────────────────────────────────────────────────────────
// PLCopen 坐标 → 场景坐标放大系数
// ─────────────────────────────────────────────────────────────
static constexpr qreal kScale = 2.0;

// ─────────────────────────────────────────────────────────────
// SFC 颜色（FBD/LD 用各 item 自身配色）
// ─────────────────────────────────────────────────────────────
static const QColor kColStep     { "#6A1B9A" };
static const QColor kColStepFill { "#F3E5F5" };
static const QColor kColTrans    { "#1A2E4A" };
static const QColor kColBlock    { "#1A2E4A" };
static const QColor kColWire     { "#1A2E4A" };

PlcOpenViewer::PlcOpenViewer(QObject* parent)
    : QGraphicsScene(parent)
{
    setSceneRect(-80, -80, 2200, 2000);
}

// ─────────────────────────────────────────────────────────────
void PlcOpenViewer::loadFromXmlString(const QString& xmlBody)
{
    clear();
    m_outPort.clear();
    m_namedOutPort.clear();
    m_items.clear();

    int nl = xmlBody.indexOf('\n');
    const QString lang = xmlBody.left(nl).trimmed().toUpper();
    const QString xml  = xmlBody.mid(nl + 1);

    QDomDocument doc;
    if (!doc.setContent(xml)) {
        addText("[ failed to parse PLCopen XML ]");
        return;
    }

    QDomElement body = doc.documentElement();
    if (body.isNull()) {
        addText("[ empty body ]");
        return;
    }

    if (lang == "SFC")
        buildSfc(body);
    else
        buildFbd(body);
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

// ─────────────────────────────────────────────────────────────
// 辅助：从 QPainterPath 创建导线图形项
// ─────────────────────────────────────────────────────────────
static QGraphicsPathItem* makeWirePath(const QPainterPath& path)
{
    auto* item = new QGraphicsPathItem(path);
    QPen pen(kColWire, 1.5);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    item->setPen(pen);
    item->setFlag(QGraphicsItem::ItemIsSelectable, true);
    return item;
}

// ═══════════════════════════════════════════════════════════════
// FBD / LD 渲染
// ═══════════════════════════════════════════════════════════════
void PlcOpenViewer::buildFbd(const QDomElement& body)
{
    createFbdItems(body);
    drawFbdWires(body);
}

void PlcOpenViewer::createFbdItems(const QDomElement& body)
{
    for (QDomElement e = body.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement())
    {
        const QString tag = e.tagName();
        int lid = e.attribute("localId", "-1").toInt();
        QPointF p = absPos(e);

        // ── 功能块（block）──────────────────────────────────────
        if (tag == "block") {
            const QString typeName     = e.attribute("typeName");
            const QString instanceName = e.attribute("instanceName");

            // 从 PLCopen XML 解析实际使用的端口（不用 defaultPorts 硬编码）
            QStringList inNames, outNames;
            QDomElement inVarsEl = e.firstChildElement("inputVariables");
            QDomNodeList inListEl = inVarsEl.elementsByTagName("variable");
            for (int k = 0; k < inListEl.count(); ++k)
                inNames << inListEl.at(k).toElement().attribute("formalParameter");

            QDomElement outVarsEl = e.firstChildElement("outputVariables");
            QDomNodeList outListEl = outVarsEl.elementsByTagName("variable");
            for (int k = 0; k < outListEl.count(); ++k)
                outNames << outListEl.at(k).toElement().attribute("formalParameter");

            auto* fb = new FunctionBlockItem(typeName, instanceName);
            fb->setCustomPorts(inNames, outNames);   // 替换为 PLCopen 实际端口
            // 设 pos 前不在场景中 → 不触发 itemChange 吸附
            fb->setPos(p);
            addItem(fb);
            m_items[lid] = fb;

            // 记录各输出端口的场景坐标（按 formalParameter 名索引）
            for (int k = 0; k < outNames.size(); ++k) {
                const QString param = outNames[k];
                // setCustomPorts 已把 outNames 写入 m_outputs，indexOf 可用
                int idx = fb->outputPortIndex(param);
                if (idx >= 0) {
                    QPointF portPt = fb->outputPortPos(idx);
                    m_namedOutPort[lid][param] = portPt;
                    if (k == 0) m_outPort[lid] = portPt;
                }
            }
        }

        // ── contact（触点）─────────────────────────────────────
        else if (tag == "contact") {
            const QString varName = e.firstChildElement("variable").text();
            bool negated = (e.attribute("negated","false") == "true");

            auto* ct = new ContactItem(
                negated ? ContactItem::NormalClosed : ContactItem::NormalOpen);
            ct->setTagName(varName);
            ct->setPos(p);
            addItem(ct);
            m_items[lid] = ct;

            // 输出端口 = rightPort()
            m_outPort[lid] = ct->rightPort();
        }

        // ── inVariable (输入变量/常量)──────────────────────────
        else if (tag == "inVariable") {
            const QString expr = e.firstChildElement("expression").text();

            auto* vb = new VarBoxItem(expr, VarBoxItem::InVar);
            vb->setPos(p);
            addItem(vb);
            m_items[lid] = vb;
            m_outPort[lid] = vb->rightPort();
        }

        // ── outVariable (输出变量) ─────────────────────────────
        else if (tag == "outVariable") {
            const QString expr = e.firstChildElement("expression").text();

            auto* vb = new VarBoxItem(expr, VarBoxItem::OutVar);
            vb->setPos(p);
            addItem(vb);
            m_items[lid] = vb;
        }

        // ── inOutVariable (双向变量) ───────────────────────────
        else if (tag == "inOutVariable") {
            const QString expr = e.firstChildElement("expression").text();

            auto* vb = new VarBoxItem(expr, VarBoxItem::InOutVar);
            vb->setPos(p);
            addItem(vb);
            m_items[lid] = vb;
            m_outPort[lid] = vb->rightPort();
        }

        // ── leftPowerRail（左母线）────────────────────────────
        else if (tag == "leftPowerRail") {
            double h = e.attribute("height","40").toDouble() * kScale;
            auto* r = new QGraphicsRectItem(p.x(), p.y(), 8, h);
            r->setPen(QPen(QColor("#1565C0"), 2));
            r->setBrush(QBrush(QColor("#1565C0")));
            addItem(r);

            // 输出端口：右侧中央
            m_outPort[lid] = QPointF(p.x() + 8, p.y() + h / 2.0);
        }

        // ── comment（注释框）──────────────────────────────────
        else if (tag == "comment") {
            double w = e.attribute("width","100").toDouble() * kScale;
            double h = e.attribute("height","40").toDouble() * kScale;
            QDomElement content = e.firstChildElement("content");
            QString txt;
            for (QDomElement ch = content.firstChildElement();
                 !ch.isNull(); ch = ch.nextSiblingElement()) {
                txt = ch.text().trimmed(); break;
            }
            auto* r = new QGraphicsRectItem(p.x(), p.y(), w, h);
            r->setPen(QPen(QColor("#BBBBBB"), 1, Qt::DashLine));
            r->setBrush(QBrush(QColor("#FFFDE7")));
            addItem(r);

            auto* t = new QGraphicsTextItem();
            QFont f("Arial", 8);
            t->setFont(f);
            t->setTextWidth(w - 6);
            t->setPlainText(txt);
            t->setDefaultTextColor(QColor("#666"));
            t->setPos(p.x() + 3, p.y() + 3);
            addItem(t);
        }
    }
}

void PlcOpenViewer::drawFbdWires(const QDomElement& body)
{
    // ── 源端口坐标查找 ────────────────────────────────────────
    auto srcPort = [&](int refId, const QString& fp) -> QPointF {
        if (!fp.isEmpty() && m_namedOutPort.contains(refId)) {
            const auto& nm = m_namedOutPort[refId];
            if (nm.contains(fp)) return nm[fp];
        }
        if (m_outPort.contains(refId)) return m_outPort[refId];
        return QPointF(-1e9, -1e9);
    };

    // ── 目标（输入）端口坐标查找 ──────────────────────────────
    auto dstPort = [&](int dstId, const QString& fp) -> QPointF {
        QGraphicsItem* gi = m_items.value(dstId, nullptr);
        if (!gi) return QPointF(-1e9, -1e9);
        if (auto* fb = qgraphicsitem_cast<FunctionBlockItem*>(gi)) {
            int idx = fb->inputPortIndex(fp);
            if (idx >= 0) return fb->inputPortPos(idx);
            // fallback: first input port
            if (fb->inputCount() > 0) return fb->inputPortPos(0);
        }
        if (auto* ct = qgraphicsitem_cast<ContactItem*>(gi))
            return ct->leftPort();
        if (auto* vb = qgraphicsitem_cast<VarBoxItem*>(gi))
            return vb->leftPort();
        return QPointF(-1e9, -1e9);
    };

    // ── 收集所有 item 的包围矩形（用于避障）─────────────────
    QVector<QRectF> blockRects;
    qreal maxBottom = 0.0;
    for (auto* gi : m_items) {
        QRectF r = gi->sceneBoundingRect().adjusted(-4, -4, 4, 4);
        blockRects.append(r);
        maxBottom = qMax(maxBottom, r.bottom());
    }
    // 反馈线的水平走线 Y（所有块下方 50px）
    const qreal kRouteY = maxBottom + 50.0;

    // ── 检查从 (x, y0) 到 (x, y1) 的竖线是否穿过某块 ────────
    auto hitsBlock = [&](qreal x, qreal y0, qreal y1) -> bool {
        if (y0 > y1) std::swap(y0, y1);
        for (const QRectF& r : blockRects) {
            if (x > r.left() && x < r.right() &&
                y1 > r.top()  && y0 < r.bottom())
                return true;
        }
        return false;
    };

    // ── 找到 x 右侧第一个不与任何块相交的 x 坐标 ────────────
    auto clearXRight = [&](qreal x, qreal y0, qreal y1) -> qreal {
        const int MaxIter = 20;
        for (int i = 0; i < MaxIter && hitsBlock(x, y0, y1); ++i) {
            qreal newX = x;
            for (const QRectF& r : blockRects) {
                if (x > r.left() && x < r.right())
                    newX = qMax(newX, r.right() + 8.0);
            }
            if (newX <= x) break;
            x = newX;
        }
        return x;
    };

    // ── 正交路由：H-V-H（或反馈线绕底部）────────────────────
    auto route = [&](QPointF s, QPointF d) -> QPainterPath {
        QPainterPath path;
        path.moveTo(s);

        const bool backward = (s.x() > d.x() + 5.0);

        if (backward) {
            // 反馈/环回线：向下→水平走→向上→到达目标
            path.lineTo(s.x(), kRouteY);
            path.lineTo(d.x(), kRouteY);
            path.lineTo(d);
        } else {
            // 正向线：先水平到中点，再竖直，再水平到目标
            qreal midX = (s.x() + d.x()) / 2.0;

            // 如果中点竖线穿过某个块，把 midX 推到块右侧
            midX = clearXRight(midX, s.y(), d.y());

            path.lineTo(midX, s.y());
            path.lineTo(midX, d.y());
            path.lineTo(d);
        }
        return path;
    };

    // ── 遍历所有元素，处理 connectionPointIn ─────────────────
    for (QDomElement e = body.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement())
    {
        const QString tag = e.tagName();
        int lid = e.attribute("localId","-1").toInt();

        auto processConn = [&](const QDomElement& cpIn,
                                const QString& dstFp) {
            QPointF dst = dstPort(lid, dstFp);
            if (dst.x() < -1e8) return;

            QDomNodeList conns = cpIn.elementsByTagName("connection");
            for (int k = 0; k < conns.count(); ++k) {
                QDomElement conn = conns.at(k).toElement();
                int refId = conn.attribute("refLocalId","-1").toInt();
                QString fp = conn.attribute("formalParameter","");
                QPointF src = srcPort(refId, fp);
                if (src.x() < -1e8) continue;

                addItem(makeWirePath(route(src, dst)));
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
        else if (tag == "outVariable" || tag == "inOutVariable") {
            processConn(e.firstChildElement("connectionPointIn"), "");
        }
        else if (tag == "contact") {
            processConn(e.firstChildElement("connectionPointIn"), "");
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// SFC 渲染（保持原来的简单矩形/线条方式）
// ═══════════════════════════════════════════════════════════════
void PlcOpenViewer::buildSfc(const QDomElement& body)
{
    createSfcItems(body);
    drawSfcWires(body);
}

QGraphicsTextItem* PlcOpenViewer::addLabel(const QString& text,
                                            const QRectF& rect,
                                            Qt::AlignmentFlag,
                                            int fontSize)
{
    auto* item = new QGraphicsTextItem(text);
    QFont f("Arial", fontSize);
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

            // 转换条件
            QDomElement cond = e.firstChildElement("condition")
                                   .firstChildElement("inline");
            if (!cond.isNull()) {
                QDomNodeList ps = cond.firstChildElement("ST").childNodes();
                for (int k = 0; k < ps.count(); ++k) {
                    QDomElement pe = ps.at(k).toElement();
                    if (!pe.isNull()) {
                        auto* t = new QGraphicsTextItem(pe.text().trimmed());
                        QFont f("Arial", 8); t->setFont(f);
                        t->setPos(cx + 18, p.y() - 10);
                        addItem(t);
                        break;
                    }
                }
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
            QFont f("Arial",8); t->setFont(f);
            t->setPos(p.x()+w/2+10, p.y()); addItem(t);
        }
        else if (tag == "actionBlock") {
            QString allActions;
            QDomNodeList actions = e.elementsByTagName("action");
            for (int k = 0; k < actions.count(); ++k) {
                QDomElement ac = actions.at(k).toElement();
                QDomNodeList ps = ac.firstChildElement("inline")
                                     .firstChildElement("ST").childNodes();
                for (int m2 = 0; m2 < ps.count(); ++m2) {
                    QDomElement pe = ps.at(m2).toElement();
                    if (!pe.isNull()) {
                        allActions += pe.text().trimmed() + "\n"; break;
                    }
                }
            }
            auto* r = new QGraphicsRectItem(rect);
            r->setPen(QPen(kColBlock,1.2));
            r->setBrush(QBrush(QColor("#FFF9C4")));
            r->setFlag(QGraphicsItem::ItemIsSelectable, true);
            addItem(r);
            auto* t = new QGraphicsTextItem();
            QFont f("Courier New", 8); t->setFont(f);
            t->setTextWidth(rect.width()-4);
            t->setPlainText(allActions.trimmed());
            t->setPos(rect.x()+2, rect.y()+2);
            addItem(t);
        }
    }
}

void PlcOpenViewer::drawSfcWires(const QDomElement& body)
{
    QPen wirePen(kColTrans, 1.5);

    auto srcPort = [&](int refId) -> QPointF {
        return m_outPort.value(refId, QPointF(-1e9,-1e9));
    };

    for (QDomElement e = body.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement())
    {
        QDomElement cpIn = e.firstChildElement("connectionPointIn");
        if (cpIn.isNull()) continue;

        QDomElement relPos = cpIn.firstChildElement("relPosition");
        QPointF ePos = absPos(e);
        QPointF dst(ePos.x() + relPos.attribute("x","0").toDouble()*kScale,
                    ePos.y() + relPos.attribute("y","0").toDouble()*kScale);

        QDomNodeList conns = cpIn.elementsByTagName("connection");
        for (int k = 0; k < conns.count(); ++k) {
            QDomElement conn = conns.at(k).toElement();
            int refId = conn.attribute("refLocalId","-1").toInt();
            QPointF src = srcPort(refId);
            if (src.x() < -1e8) continue;

            auto* pi = new QGraphicsPathItem();
            QPainterPath path;
            path.moveTo(src);
            path.lineTo(dst);
            pi->setPath(path);
            pi->setPen(wirePen);
            addItem(pi);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// 编辑功能：背景 / 前景 / 事件处理
// ═══════════════════════════════════════════════════════════════

// ── 空白画布初始化 ─────────────────────────────────────────────
void PlcOpenViewer::initEmpty()
{
    clear();
    m_outPort.clear();
    m_namedOutPort.clear();
    m_items.clear();

    // 左电源母线（放在左上角，用户可自由移动）
    auto* rail = new QGraphicsRectItem(40, 40, 8, 240);
    rail->setPen(QPen(QColor("#1565C0"), 2));
    rail->setBrush(QBrush(QColor("#1565C0")));
    rail->setFlag(QGraphicsItem::ItemIsSelectable, true);
    rail->setFlag(QGraphicsItem::ItemIsMovable, true);
    addItem(rail);
}

// ── 点阵背景（无横线，Beremiz 风格）──────────────────────────
void PlcOpenViewer::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, QColor("#FFFFFF"));

    painter->setPen(QPen(QColor("#CCCCCC"), 1.0));
    const int dot = GridSize;
    int fx = (int)rect.left()  - ((int)rect.left()  % dot);
    int fy = (int)rect.top()   - ((int)rect.top()   % dot);
    for (int x = fx; x <= (int)rect.right();  x += dot)
        for (int y = fy; y <= (int)rect.bottom(); y += dot)
            painter->drawPoint(x, y);
}

// ── 端口吸附指示器（绿色圆圈）────────────────────────────────
void PlcOpenViewer::drawForeground(QPainter* painter, const QRectF&)
{
    if (!m_showPortSnap) return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(QColor("#00AA44"), 1.5));
    painter->setBrush(QColor(0, 170, 68, 60));
    painter->drawEllipse(m_portSnapPos, 9.0, 9.0);
    painter->restore();
}

// ── 设置编辑模式 ──────────────────────────────────────────────
void PlcOpenViewer::setMode(EditorMode mode)
{
    if (m_tempWire && mode != Mode_AddWire) {
        removeItem(m_tempWire);
        delete m_tempWire;
        m_tempWire = nullptr;
    }
    m_showPortSnap = false;
    m_mode = mode;
    emit modeChanged(mode);
}

// ── 查找最近端口（用于导线吸附）──────────────────────────────
QPointF PlcOpenViewer::snapToNearestPort(const QPointF& pos, qreal radius) const
{
    QPointF best = pos;
    qreal   bestSq = radius * radius;

    auto check = [&](QPointF p) {
        qreal dx = p.x() - pos.x(), dy = p.y() - pos.y();
        qreal d = dx*dx + dy*dy;
        if (d < bestSq) { bestSq = d; best = p; }
    };

    for (QGraphicsItem* gi : items()) {
        if (auto* bi = dynamic_cast<BaseItem*>(gi)) {
            check(bi->leftPort());
            check(bi->rightPort());
        }
        if (auto* fb = dynamic_cast<FunctionBlockItem*>(gi)) {
            for (int i = 0; i < fb->inputCount();  ++i) check(fb->inputPortPos(i));
            for (int i = 0; i < fb->outputCount(); ++i) check(fb->outputPortPos(i));
        }
    }
    return best;
}

// ── 鼠标按下：放置元件 / 画导线 ──────────────────────────────
void PlcOpenViewer::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_mode == Mode_Select) {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    const QPointF raw = event->scenePos();
    auto snapG = [&](qreal v) { return qRound(v / GridSize) * (qreal)GridSize; };
    const QPointF snapPt(snapG(raw.x()), snapG(raw.y()));

    // ── 导线模式 ──────────────────────────────────────────────
    if (m_mode == Mode_AddWire) {
        QPointF snap = snapToNearestPort(raw, 20.0);
        if (snap == raw) snap = snapPt;

        if (!m_tempWire) {
            m_tempWire = new WireItem(snap, snap);
            addItem(m_tempWire);
        } else {
            m_tempWire->setEndPos(snap);
            m_tempWire     = nullptr;
            m_showPortSnap = false;
            update();
        }
        return;
    }

    // ── 功能块（自由放置）────────────────────────────────────
    if (m_mode == Mode_AddFuncBlock) {
        auto* fb = new FunctionBlockItem(
            "TON", QString("TON_%1").arg(m_fbCount++));
        fb->setPos(snapPt);
        addItem(fb);
        return;
    }

    // ── 触点 / 线圈（吸附网格，自由 Y）──────────────────────
    BaseItem* newItem = nullptr;
    switch (m_mode) {
    case Mode_AddContact_NO:
        newItem = new ContactItem(ContactItem::NormalOpen);
        static_cast<ContactItem*>(newItem)->setTagName(
            QString("X%1").arg(m_contactCount++));
        break;
    case Mode_AddContact_NC:
        newItem = new ContactItem(ContactItem::NormalClosed);
        static_cast<ContactItem*>(newItem)->setTagName(
            QString("X%1").arg(m_contactCount++));
        break;
    case Mode_AddContact_P:
        newItem = new ContactItem(ContactItem::PositiveTransition);
        static_cast<ContactItem*>(newItem)->setTagName(
            QString("X%1").arg(m_contactCount++));
        break;
    case Mode_AddContact_N:
        newItem = new ContactItem(ContactItem::NegativeTransition);
        static_cast<ContactItem*>(newItem)->setTagName(
            QString("X%1").arg(m_contactCount++));
        break;
    case Mode_AddCoil:
        newItem = new CoilItem(CoilItem::Output);
        static_cast<CoilItem*>(newItem)->setTagName(
            QString("Y%1").arg(m_coilCount++));
        break;
    case Mode_AddCoil_S:
        newItem = new CoilItem(CoilItem::SetCoil);
        static_cast<CoilItem*>(newItem)->setTagName(
            QString("Y%1").arg(m_coilCount++));
        break;
    case Mode_AddCoil_R:
        newItem = new CoilItem(CoilItem::ResetCoil);
        static_cast<CoilItem*>(newItem)->setTagName(
            QString("Y%1").arg(m_coilCount++));
        break;
    default:
        break;
    }

    if (newItem) {
        newItem->setPos(snapPt);
        addItem(newItem);
    }
}

// ── 鼠标移动：更新导线预览 + 端口吸附指示器 ─────────────────
void PlcOpenViewer::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsScene::mouseMoveEvent(event);

    if (m_mode == Mode_AddWire) {
        const QPointF raw  = event->scenePos();
        const QPointF snap = snapToNearestPort(raw, 20.0);

        bool hadSnap   = m_showPortSnap;
        m_showPortSnap = (snap != raw);
        m_portSnapPos  = snap;

        if (m_tempWire) {
            QPointF end = m_showPortSnap ? snap
                : QPointF(qRound(raw.x() / GridSize) * (qreal)GridSize,
                          qRound(raw.y() / GridSize) * (qreal)GridSize);
            m_tempWire->setEndPos(end);
        }

        if (hadSnap != m_showPortSnap || m_tempWire)
            update();

    } else if (m_showPortSnap) {
        m_showPortSnap = false;
        update();
    }
}

// ── 键盘：Delete 删除选中；Escape 返回 Select ─────────────────
void PlcOpenViewer::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete ||
        event->key() == Qt::Key_Backspace)
    {
        const auto sel = selectedItems();
        for (QGraphicsItem* item : sel) {
            removeItem(item);
            delete item;
        }
        if (m_tempWire && !items().contains(m_tempWire))
            m_tempWire = nullptr;
        event->accept();
    }
    else if (event->key() == Qt::Key_Escape) {
        if (m_tempWire) {
            removeItem(m_tempWire);
            delete m_tempWire;
            m_tempWire = nullptr;
        }
        m_showPortSnap = false;
        setMode(Mode_Select);
        event->accept();
    }
    else {
        QGraphicsScene::keyPressEvent(event);
    }
}

// ── 右键菜单 ──────────────────────────────────────────────────
void PlcOpenViewer::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    const QPointF scenePos = event->scenePos();
    auto snapG = [&](qreal v) { return qRound(v / GridSize) * (qreal)GridSize; };
    const QPointF snapPt(snapG(scenePos.x()), snapG(scenePos.y()));

    // 找点击位置的 BaseItem
    BaseItem* hitItem = nullptr;
    for (QGraphicsItem* gi : items(scenePos)) {
        hitItem = dynamic_cast<BaseItem*>(gi);
        if (hitItem) break;
    }

    QMenu menu;

    if (hitItem) {
        auto* editAct = menu.addAction("Edit Name...");
        menu.addSeparator();
        auto* delAct  = menu.addAction("Delete");

        QAction* chosen = menu.exec(event->screenPos());
        if (chosen == editAct)
            hitItem->editProperties();
        else if (chosen == delAct) {
            removeItem(hitItem);
            delete hitItem;
        }
    } else {
        // 空白区域：快速添加元件
        auto* addNO  = menu.addAction("Add Contact (NO)");
        auto* addNC  = menu.addAction("Add Contact (NC)");
        menu.addSeparator();
        auto* addOut = menu.addAction("Add Coil (Output)");
        auto* addSet = menu.addAction("Add Set Coil (S)");
        auto* addRst = menu.addAction("Add Reset Coil (R)");
        menu.addSeparator();
        auto* addFb  = menu.addAction("Add Function Block...");

        QAction* chosen = menu.exec(event->screenPos());

        BaseItem* newItem = nullptr;
        if (chosen == addNO) {
            auto* c = new ContactItem(ContactItem::NormalOpen);
            c->setTagName(QString("X%1").arg(m_contactCount++));
            newItem = c;
        } else if (chosen == addNC) {
            auto* c = new ContactItem(ContactItem::NormalClosed);
            c->setTagName(QString("X%1").arg(m_contactCount++));
            newItem = c;
        } else if (chosen == addOut) {
            auto* c = new CoilItem(CoilItem::Output);
            c->setTagName(QString("Y%1").arg(m_coilCount++));
            newItem = c;
        } else if (chosen == addSet) {
            auto* c = new CoilItem(CoilItem::SetCoil);
            c->setTagName(QString("Y%1").arg(m_coilCount++));
            newItem = c;
        } else if (chosen == addRst) {
            auto* c = new CoilItem(CoilItem::ResetCoil);
            c->setTagName(QString("Y%1").arg(m_coilCount++));
            newItem = c;
        } else if (chosen == addFb) {
            bool ok;
            const QString fbType = QInputDialog::getItem(
                nullptr, "Add Function Block", "Block type:",
                {"TON","TOF","CTU","CTD","CTUD","ADD","SUB","MUL","DIV","SEL","MUX","SR","RS"},
                0, false, &ok);
            if (ok) {
                auto* fb = new FunctionBlockItem(
                    fbType, QString("%1_%2").arg(fbType).arg(m_fbCount++));
                fb->setPos(snapPt);
                addItem(fb);
            }
            event->accept();
            return;
        }

        if (newItem) {
            newItem->setPos(snapPt);
            addItem(newItem);
        }
    }

    event->accept();
}
