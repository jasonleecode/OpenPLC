#include "FunctionBlockItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QInputDialog>
#include <QGraphicsSceneMouseEvent>

// ── 常用功能块的端口定义 ──────────────────────────────────────
struct FbPortDef {
    QStringList inputs;
    QStringList outputs;
};

static FbPortDef defaultPorts(const QString& type) {
    if (type == "TON" || type == "TOF")
        return { {"EN","IN","PT"}, {"ENO","Q","ET"} };
    if (type == "TONR")
        return { {"EN","IN","PT","R"}, {"ENO","Q","ET"} };
    if (type == "CTU")
        return { {"EN","CU","R","PV"}, {"ENO","Q","CV"} };
    if (type == "CTD")
        return { {"EN","CD","LD","PV"}, {"ENO","Q","CV"} };
    if (type == "CTUD")
        return { {"EN","CU","CD","R","LD","PV"}, {"ENO","QU","QD","CV"} };
    if (type == "ADD" || type == "SUB" || type == "MUL" || type == "DIV")
        return { {"EN","IN1","IN2"}, {"ENO","OUT"} };
    if (type == "SEL")
        return { {"EN","G","IN0","IN1"}, {"ENO","OUT"} };
    if (type == "MUX")
        return { {"EN","K","IN0","IN1","IN2"}, {"ENO","OUT"} };
    if (type == "SR")
        return { {"EN","S1","R"}, {"ENO","Q1"} };
    if (type == "RS")
        return { {"EN","S","R1"}, {"ENO","Q1"} };
    // 默认：两输入两输出
    return { {"EN","IN"}, {"ENO","OUT"} };
}

// ─────────────────────────────────────────────────────────────

FunctionBlockItem::FunctionBlockItem(const QString& blockType,
                                     const QString& instanceName,
                                     QGraphicsItem *parent)
    : BaseItem(parent),
      m_blockType(blockType),
      m_instanceName(instanceName)
{
    rebuildPorts();
    setToolTip(QString("%1 — Double-click to edit").arg(blockType));
}

void FunctionBlockItem::rebuildPorts() {
    auto def = defaultPorts(m_blockType);
    m_inputs  = def.inputs;
    m_outputs = def.outputs;
    prepareGeometryChange();
}

int FunctionBlockItem::boxHeight() const {
    return HeaderH + qMax(m_inputs.size(), m_outputs.size()) * PortRowH + 8;
}

QRectF FunctionBlockItem::boundingRect() const {
    if (m_hasXmlGeom)
        return QRectF(-PortLineW, 0, m_xmlW + 2 * PortLineW, m_xmlH);
    return QRectF(-PortLineW, 0,
                  BoxWidth + 2 * PortLineW,
                  boxHeight());
}

void FunctionBlockItem::paint(QPainter *painter,
                               const QStyleOptionGraphicsItem *option,
                               QWidget*)
{
    const bool selected = (option->state & QStyle::State_Selected);
    const QColor borderColor = selected ? QColor("#0078D7") : QColor("#2A2A2A");
    const QColor fillColor   = QColor("#FAFCFF");
    const QColor headerColor = QColor("#DDE8F5");

    // ── XML 几何模式：按 XML 尺寸缩放绘制 ────────────────────
    if (m_hasXmlGeom) {
        const qreal bw = m_xmlW, bh2 = m_xmlH;
        const qreal hH = qMin(bh2 * 0.35, 28.0);

        // 主框
        painter->setPen(QPen(borderColor, selected ? 2.0 : 1.5));
        painter->setBrush(fillColor);
        painter->drawRect(0, 0, bw, bh2);

        // 标题带
        painter->setBrush(headerColor);
        painter->setPen(Qt::NoPen);
        painter->drawRect(1, 1, bw - 2, hH - 1);
        painter->setPen(QPen(borderColor, 1));
        painter->drawLine(QPointF(0, hH), QPointF(bw, hH));

        // 类型名（字体上限 14px，由已封顶的 hH≤28 保证）
        QFont tf("Consolas, Courier New");
        tf.setBold(true);
        tf.setPixelSize(qMax(7, (int)(hH * 0.5)));
        painter->setFont(tf);
        painter->setPen(QColor("#1A2E4A"));
        painter->drawText(QRectF(2, 1, bw - 4, hH - 2), Qt::AlignCenter, m_blockType);

        // 实例名（跟随 hH，避免大块时字体爆炸）
        if (bh2 - hH > 10) {
            QFont sf("Consolas, Courier New");
            sf.setItalic(true);
            sf.setPixelSize(qMax(6, (int)(hH * 0.4)));   // 与 hH 挂钩，上限约 11px
            painter->setFont(sf);
            painter->setPen(QColor("#555555"));
            painter->drawText(QRectF(2, hH + 1, bw - 4, hH),
                              Qt::AlignCenter, m_instanceName);
        }

        // 端口连线+标签（固定 9px，不随块高缩放）
        QFont pf("Consolas, Courier New");
        pf.setPixelSize(9);
        painter->setFont(pf);
        for (int i = 0; i < m_xmlInPorts.size(); ++i) {
            const QPointF pt = m_xmlInPorts[i];
            painter->setPen(QPen(borderColor, 1.5));
            painter->drawLine(QPointF(-PortLineW, pt.y()), QPointF(pt.x(), pt.y()));
            painter->setPen(Qt::NoPen);
            painter->setBrush(borderColor);
            painter->drawEllipse(QPointF(-PortLineW, pt.y()), 2.5, 2.5);
            painter->setBrush(Qt::NoBrush);
            if (i < m_inputs.size()) {
                painter->setPen(QColor("#333333"));
                painter->drawText(QRectF(pt.x() + 2, pt.y() - 7, bw * 0.5 - 4, 14),
                                  Qt::AlignLeft | Qt::AlignVCenter, m_inputs[i]);
            }
        }
        for (int i = 0; i < m_xmlOutPorts.size(); ++i) {
            const QPointF pt = m_xmlOutPorts[i];
            painter->setPen(QPen(borderColor, 1.5));
            painter->drawLine(QPointF(pt.x(), pt.y()), QPointF(pt.x() + PortLineW, pt.y()));
            painter->setPen(Qt::NoPen);
            painter->setBrush(borderColor);
            painter->drawEllipse(QPointF(pt.x() + PortLineW, pt.y()), 2.5, 2.5);
            painter->setBrush(Qt::NoBrush);
            if (i < m_outputs.size()) {
                painter->setPen(QColor("#333333"));
                painter->drawText(QRectF(bw * 0.5, pt.y() - 7, pt.x() - bw * 0.5 - 2, 14),
                                  Qt::AlignRight | Qt::AlignVCenter, m_outputs[i]);
            }
        }

        if (selected) {
            painter->setPen(QPen(QColor("#0078D7"), 2, Qt::DashLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(-1, -1, bw + 2, bh2 + 2);
        }
        return;
    }

    const int bh = boxHeight();

    // ── 1. 方框主体 ───────────────────────────────────────────
    painter->setPen(QPen(borderColor, selected ? 2.0 : 1.5));
    painter->setBrush(fillColor);
    painter->drawRect(0, 0, BoxWidth, bh);

    // ── 2. 标题区（蓝色背景条）──────────────────────────────
    painter->setBrush(headerColor);
    painter->setPen(Qt::NoPen);
    painter->drawRect(1, 1, BoxWidth - 2, HeaderH - 1);

    // 分隔线
    painter->setPen(QPen(borderColor, 1));
    painter->drawLine(0, HeaderH, BoxWidth, HeaderH);

    // 功能块类型名（大号加粗）
    QFont typeFont;
    typeFont.setFamily("Consolas, Courier New");
    typeFont.setPixelSize(14);
    typeFont.setBold(true);
    painter->setFont(typeFont);
    painter->setPen(QColor("#1A2E4A"));
    painter->drawText(QRectF(4, 2, BoxWidth - 8, 20),
                      Qt::AlignCenter, m_blockType);

    // 实例名（小号斜体）
    QFont instFont;
    instFont.setFamily("Consolas, Courier New");
    instFont.setPixelSize(11);
    instFont.setItalic(true);
    painter->setFont(instFont);
    painter->setPen(QColor("#555555"));
    painter->drawText(QRectF(4, 22, BoxWidth - 8, 18),
                      Qt::AlignCenter, m_instanceName);

    // ── 3. 端口行 ─────────────────────────────────────────────
    QFont portFont;
    portFont.setFamily("Consolas, Courier New");
    portFont.setPixelSize(10);
    painter->setFont(portFont);

    int portRows = qMax(m_inputs.size(), m_outputs.size());
    for (int i = 0; i < portRows; ++i) {
        int cy = HeaderH + i * PortRowH + PortRowH / 2;

        // 输入端口
        if (i < m_inputs.size()) {
            // 引线（从边框向外延伸）
            painter->setPen(QPen(borderColor, 1.5));
            painter->drawLine(-PortLineW, cy, 0, cy);
            // 端口名标签
            painter->setPen(QColor("#333333"));
            painter->drawText(QRectF(3, cy - 9, BoxWidth / 2 - 6, 18),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              m_inputs[i]);
            // 端口点
            painter->setPen(Qt::NoPen);
            painter->setBrush(borderColor);
            painter->drawEllipse(QPointF(-PortLineW, cy), 2.5, 2.5);
            painter->setBrush(Qt::NoBrush);
        }

        // 输出端口
        if (i < m_outputs.size()) {
            painter->setPen(QPen(borderColor, 1.5));
            painter->drawLine(BoxWidth, cy, BoxWidth + PortLineW, cy);
            painter->setPen(QColor("#333333"));
            painter->drawText(QRectF(BoxWidth / 2 + 3, cy - 9, BoxWidth / 2 - 6, 18),
                              Qt::AlignRight | Qt::AlignVCenter,
                              m_outputs[i]);
            painter->setPen(Qt::NoPen);
            painter->setBrush(borderColor);
            painter->drawEllipse(QPointF(BoxWidth + PortLineW, cy), 2.5, 2.5);
            painter->setBrush(Qt::NoBrush);
        }
    }

    // ── 4. 选中时的高亮边框 ───────────────────────────────────
    if (selected) {
        painter->setPen(QPen(QColor("#0078D7"), 2, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(-1, -1, BoxWidth + 2, bh + 2);
    }
}

// ── 端口位置 ──────────────────────────────────────────────────
QPointF FunctionBlockItem::inputPortPos(int i) const {
    if (m_hasXmlGeom && i < m_xmlInPorts.size())
        return mapToScene(m_xmlInPorts[i]);
    int cy = HeaderH + i * PortRowH + PortRowH / 2;
    return mapToScene(-PortLineW, cy);
}

QPointF FunctionBlockItem::outputPortPos(int i) const {
    if (m_hasXmlGeom && i < m_xmlOutPorts.size())
        return mapToScene(m_xmlOutPorts[i]);
    int cy = HeaderH + i * PortRowH + PortRowH / 2;
    return mapToScene(BoxWidth + PortLineW, cy);
}

void FunctionBlockItem::setXmlGeometry(qreal w, qreal h,
                                        const QVector<QPointF>& inPorts,
                                        const QVector<QPointF>& outPorts)
{
    m_xmlW = w; m_xmlH = h;
    m_xmlInPorts  = inPorts;
    m_xmlOutPorts = outPorts;
    m_hasXmlGeom  = true;
    prepareGeometryChange();
    update();
}

QPointF FunctionBlockItem::leftPort()  const { return inputPortPos(0); }
QPointF FunctionBlockItem::rightPort() const { return outputPortPos(0); }

// ── 属性编辑 ──────────────────────────────────────────────────
void FunctionBlockItem::setBlockType(const QString& t) {
    m_blockType = t;
    rebuildPorts();
    setToolTip(QString("%1 — Double-click to edit").arg(t));
    update();
}

void FunctionBlockItem::setCustomPorts(const QStringList& inputs,
                                        const QStringList& outputs)
{
    m_inputs  = inputs;
    m_outputs = outputs;
    prepareGeometryChange();
    update();
}

void FunctionBlockItem::setInstanceName(const QString& n) {
    m_instanceName = n;
    update();
}

void FunctionBlockItem::editProperties() {
    bool ok;
    const QString name = QInputDialog::getText(
        nullptr, "Edit Function Block",
        "Instance name:", QLineEdit::Normal, m_instanceName, &ok);
    if (ok && !name.isEmpty())
        setInstanceName(name);
}
