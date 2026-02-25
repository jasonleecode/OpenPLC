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
#include <QTimer>
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

    // timer 保留供将来手动触发动态导线更新，但不自动连接 changed 信号
    // （QGraphicsScene::changed 在 event loop 下一轮才发出，会在 loadFromXmlString
    //  返回后覆盖 XML 路径，因此改为按需手动调用 updateAllWires）
    m_wireTimer = new QTimer(this);
    m_wireTimer->setSingleShot(true);
    m_wireTimer->setInterval(0);
    connect(m_wireTimer, &QTimer::timeout,
            this, &PlcOpenViewer::updateAllWires);
}

// ─────────────────────────────────────────────────────────────
void PlcOpenViewer::loadFromXmlString(const QString& xmlBody)
{
    // 先清理连接表（wire 指针即将因 clear() 失效）
    m_connections.clear();
    clear();
    m_outPort.clear();
    m_namedOutPort.clear();
    m_items.clear();

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

    // 根据实际内容更新 sceneRect，确保滚动条能覆盖所有元素
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
        // XML 元素尺寸（已 × kScale）
        qreal xw = e.attribute("width",  "80").toDouble() * kScale;
        qreal xh = e.attribute("height", "30").toDouble() * kScale;

        // ── 功能块（block）──────────────────────────────────────
        if (tag == "block") {
            const QString typeName     = e.attribute("typeName");
            const QString instanceName = e.attribute("instanceName");

            // 解析端口名称及 relPosition
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

            // 输出端口绝对坐标（从 XML relPos 计算，确保与导线端点一致）
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

            // 输出端口：从 XML cpOut relPos
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
            const QString storage = e.attribute("storage"); // "set" / "reset" / ""
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
            // 使用 setPos 而非绝对坐标，以便 pos() 能反映位置（用于序列化）
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

            // 输出端口：从 XML cpOut relPos
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
            // 字体大小与方框高度成比例（约5%），最小 18 场景像素
            // 保证在 fitInView 常规缩放下（~0.35x）仍可读
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
    // H-V-H fallback 路由（XML 无位置信息时使用）
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

                auto* wire = new QGraphicsPathItem();
                QPen pen(kColWire, 1.5);
                pen.setJoinStyle(Qt::RoundJoin);
                pen.setCapStyle(Qt::RoundCap);
                wire->setPen(pen);
                wire->setFlag(QGraphicsItem::ItemIsSelectable, true);
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
                    // fallback：无位置信息时用端口坐标 H-V-H 路由
                    QPointF src = getOutputPortScene(refId, fp);
                    QPointF dst = getInputPortScene(lid, dstFp);
                    if (src.x() > -1e8 && dst.x() > -1e8)
                        wire->setPath(routeHvh(src, dst));
                }

                // 记录连接，供 item 被拖动后 updateAllWires() 动态重路由
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
    // 不在此处调用 updateAllWires()：
    // 初始显示使用 XML 路径，拖动后由 timer 触发 updateAllWires() 切换动态路由
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
                                            int /*fontSize*/)
{
    auto* item = new QGraphicsTextItem(text);
    // 字体与所在矩形高度成比例（约 18%），最小 10 场景像素
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

            // 转换条件：优先 <inline>，其次 <reference name="...">
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
                f.setPixelSize(18);   // transition 旁的条件文本，固定 18 场景像素
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
            f.setPixelSize(18);   // jumpStep 跳转目标名，固定 18 场景像素
            t->setFont(f);
            t->setPos(p.x()+w/2+10, p.y()); addItem(t);
        }
        else if (tag == "actionBlock") {
            // 解析每个 action：qualifier | name/code | duration
            // action 的 body 可以是 <reference name="..."> 或 <inline><ST>...</ST></inline>
            QStringList lines;
            QDomNodeList actions = e.elementsByTagName("action");
            for (int k = 0; k < actions.count(); ++k) {
                QDomElement ac = actions.at(k).toElement();
                QString qualifier = ac.attribute("qualifier");
                QString duration  = ac.attribute("duration");

                // 动作名称：优先 <reference name>, 其次 <inline> ST 代码
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

                // 格式：Q | name | duration（duration 可选）
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
            // actionBlock 字体与方框高度成比例（约5%），最小 14 场景像素
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
        // 遍历所有 connectionPointIn（selectionConvergence 有多个）
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

                // 优先使用 XML 存储的折点（顺序 dst→src，绘制时反转）
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
                    // fallback：直线（无 XML 位置数据时）
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
// 编辑功能：背景 / 前景 / 事件处理
// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
// 动态导线路由 & 序列化
// ═══════════════════════════════════════════════════════════════

// ── 获取 item 的输出端口场景坐标 ──────────────────────────────
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

    // leftPowerRail（raw QGraphicsRectItem）：使用 sceneBoundingRect 右侧中心
    QRectF r = gi->sceneBoundingRect();
    return { r.right(), r.center().y() };
}

// ── 获取 item 的输入端口场景坐标 ──────────────────────────────
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

// ── 重算所有导线路径（H-V-H 正交路由）────────────────────────
void PlcOpenViewer::updateAllWires()
{
    if (m_updatingWires) return;
    m_updatingWires = true;

    for (FbdConn& c : m_connections) {
        if (!c.wire) continue;
        QPointF src = getOutputPortScene(c.srcId, c.srcParam);
        QPointF dst = getInputPortScene (c.dstId, c.dstParam);
        if (src.x() < -1e8 || dst.x() < -1e8) {
            c.wire->setPath(QPainterPath());
            continue;
        }
        QPainterPath path;
        path.moveTo(src);
        qreal midX = (src.x() + dst.x()) / 2.0;
        path.lineTo(midX, src.y());
        path.lineTo(midX, dst.y());
        path.lineTo(dst);
        c.wire->setPath(path);
    }

    m_updatingWires = false;
}

// ── 在 DOM 中按 localId 查找顶层元素 ─────────────────────────
QDomElement PlcOpenViewer::findElemById(const QDomElement& root, int lid)
{
    for (QDomElement e = root.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement()) {
        if (e.attribute("localId", "-1").toInt() == lid) return e;
    }
    return {};
}

// ── 把当前 item 坐标写回 m_bodyDoc 的 <position> ─────────────
void PlcOpenViewer::syncPositionsToDoc()
{
    if (m_bodyDoc.isNull()) return;
    QDomElement root = m_bodyDoc.documentElement();

    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        QDomElement elem = findElemById(root, it.key());
        if (elem.isNull()) continue;

        // pos() 给出 item 在场景中的位置（setPos 设置的值）
        QPointF p = it.value()->pos() / kScale;
        QDomElement posEl = elem.firstChildElement("position");
        if (!posEl.isNull()) {
            posEl.setAttribute("x", QString::number(qRound(p.x())));
            posEl.setAttribute("y", QString::number(qRound(p.y())));
        }
    }
}

// ── 把当前导线折点写回 m_bodyDoc 的 <connection>/<position> ──
void PlcOpenViewer::syncWirePathsToDoc()
{
    if (m_bodyDoc.isNull()) return;
    QDomElement root = m_bodyDoc.documentElement();

    for (const FbdConn& c : m_connections) {
        if (!c.wire) continue;
        QPainterPath path = c.wire->path();
        if (path.elementCount() < 2) continue;

        // 找到目标元素
        QDomElement dstElem = findElemById(root, c.dstId);
        if (dstElem.isNull()) continue;

        // 找到对应的 <connection> 子元素
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

        // 删除旧 <position> 子元素
        QList<QDomNode> toRemove;
        QDomNodeList posElems = connElem.childNodes();
        for (int k = 0; k < posElems.count(); ++k) {
            QDomElement pe = posElems.at(k).toElement();
            if (!pe.isNull() && pe.tagName() == "position")
                toRemove << pe;
        }
        for (QDomNode n : toRemove)
            connElem.removeChild(n);

        // 写入新 <position>（PLCopen 顺序：dst→src，即路径倒序）
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

// ── 序列化场景 → "FBD\n<FBD>...</FBD>" ──────────────────────
QString PlcOpenViewer::toXmlString()
{
    if (m_bodyDoc.isNull() || m_bodyLanguage.isEmpty()) return {};
    syncPositionsToDoc();
    syncWirePathsToDoc();
    return m_bodyLanguage + "\n" + m_bodyDoc.toString(2);
}

// ── 空白画布初始化 ─────────────────────────────────────────────
void PlcOpenViewer::initEmpty()
{
    m_connections.clear();
    clear();
    m_outPort.clear();
    m_namedOutPort.clear();
    m_items.clear();
    m_bodyDoc      = QDomDocument();
    m_bodyLanguage = {};

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
