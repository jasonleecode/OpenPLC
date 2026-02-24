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
    int cy = HeaderH + i * PortRowH + PortRowH / 2;
    return mapToScene(-PortLineW, cy);
}

QPointF FunctionBlockItem::outputPortPos(int i) const {
    int cy = HeaderH + i * PortRowH + PortRowH / 2;
    return mapToScene(BoxWidth + PortLineW, cy);
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
