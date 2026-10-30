#include "PlcOpenViewer.h"
#include "../items/ContactItem.h"
#include "../items/CoilItem.h"
#include "../items/FunctionBlockItem.h"
#include "../items/VarBoxItem.h"
#include <QGraphicsRectItem>
#include <QSet>

namespace {
constexpr qreal scale = 2.0;
QDomElement child(QDomDocument& doc, QDomElement parent, const QString& tag)
{
    auto e = parent.firstChildElement(tag);
    if (e.isNull()) { e = doc.createElement(tag); parent.appendChild(e); }
    return e;
}
void text(QDomDocument& doc, QDomElement parent, const QString& tag, const QString& value)
{
    auto e = child(doc, parent, tag);
    while (!e.firstChild().isNull()) e.removeChild(e.firstChild());
    e.appendChild(doc.createTextNode(value));
}
void point(QDomDocument& doc, QDomElement parent, const QString& tag, QPointF p)
{
    auto e = child(doc, parent, tag);
    e.setAttribute("x", QString::number(p.x() / scale, 'g', 12));
    e.setAttribute("y", QString::number(p.y() / scale, 'g', 12));
}
void removeConnections(QDomElement cp)
{
    for (auto c = cp.firstChildElement("connection"); !c.isNull();) {
        auto next = c.nextSiblingElement("connection"); cp.removeChild(c); c = next;
    }
}
QDomElement port(QDomDocument& doc, QDomElement block, const QString& group, const QString& name)
{
    auto list = child(doc, block, group);
    for (auto v = list.firstChildElement("variable"); !v.isNull(); v = v.nextSiblingElement("variable"))
        if (v.attribute("formalParameter") == name) return v;
    auto v = doc.createElement("variable"); v.setAttribute("formalParameter", name); list.appendChild(v);
    return v;
}
}

QString PlcOpenViewer::toXmlString()
{
    if (m_bodyLanguage.isEmpty()) return {};
    // Never mutate the imported snapshot: undo can restore a previously deleted
    // item, including its unexposed PLCopen attributes and extension nodes.
    return m_bodyLanguage + "\n" + buildBodyFromScene().toString(2);
}

QDomDocument PlcOpenViewer::buildBodyFromScene() const
{
    auto doc = m_bodyDoc.cloneNode(true).toDocument();
    if (doc.documentElement().isNull()) doc.appendChild(doc.createElement(m_bodyLanguage));
    auto root = doc.documentElement();
    // SFC is currently a preview; preserve actions, transitions and extensions.
    if (m_bodyLanguage == "SFC") return doc;
    const QSet<QString> editable = {"block", "contact", "coil", "inVariable", "outVariable",
        "inOutVariable", "leftPowerRail", "rightPowerRail"};
    for (auto e = root.firstChildElement(); !e.isNull();) {
        auto next = e.nextSiblingElement();
        if (editable.contains(e.tagName()) && !m_items.contains(e.attribute("localId").toInt()))
            root.removeChild(e);
        e = next;
    }
    QMap<int, QDomElement> elements;
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        auto* item = it.value();
        if (!item || item->scene() != this) continue;
        auto e = findElemById(root, it.key());
        QString tag;
        if (dynamic_cast<ContactItem*>(item)) tag = "contact";
        else if (dynamic_cast<CoilItem*>(item)) tag = "coil";
        else if (dynamic_cast<FunctionBlockItem*>(item)) tag = "block";
        else if (auto* v = dynamic_cast<VarBoxItem*>(item))
            tag = v->role() == VarBoxItem::InVar ? "inVariable" : v->role() == VarBoxItem::OutVar ? "outVariable" : "inOutVariable";
        else if (dynamic_cast<QGraphicsRectItem*>(item)) tag = e.isNull() ? "leftPowerRail" : e.tagName();
        else continue;
        if (e.isNull()) { e = doc.createElement(tag); root.appendChild(e); }
        e.setAttribute("localId", it.key());
        point(doc, e, "position", item->pos());
        if (!e.hasAttribute("width")) e.setAttribute("width",
            (dynamic_cast<FunctionBlockItem*>(item) ? FunctionBlockItem::BoxWidth : item->boundingRect().width()) / scale);
        if (!e.hasAttribute("height")) e.setAttribute("height", item->boundingRect().bottom() / scale);
        elements.insert(it.key(), e);

        auto simplePorts = [&](bool input, bool output) {
            if (input) {
                auto cp = child(doc, e, "connectionPointIn");
                removeConnections(cp);
                point(doc, cp, "relPosition", getInputPortScene(it.key(), {}) - item->pos());
            }
            if (output) point(doc, child(doc, e, "connectionPointOut"), "relPosition",
                              getOutputPortScene(it.key(), {}) - item->pos());
        };
        if (auto* c = dynamic_cast<ContactItem*>(item)) {
            text(doc, e, "variable", c->tagName());
            // Kind is immutable in the item UI. Preserve imported combinations
            // (e.g. a negated rising edge) that its enum cannot fully represent.
            if (!e.hasAttribute("negated")) e.setAttribute("negated", c->contactType() == ContactItem::NormalClosed ? "true" : "false");
            if (!e.hasAttribute("edge")) e.setAttribute("edge", c->contactType() == ContactItem::PositiveTransition ? "rising" :
                c->contactType() == ContactItem::NegativeTransition ? "falling" : "none");
            simplePorts(true, true);
        } else if (auto* c = dynamic_cast<CoilItem*>(item)) {
            text(doc, e, "variable", c->tagName());
            if (!e.hasAttribute("negated")) e.setAttribute("negated", c->coilType() == CoilItem::Negated ? "true" : "false");
            if (!e.hasAttribute("storage")) e.setAttribute("storage", c->coilType() == CoilItem::SetCoil ? "set" :
                c->coilType() == CoilItem::ResetCoil ? "reset" : "none");
            simplePorts(true, true);
        } else if (auto* v = dynamic_cast<VarBoxItem*>(item)) {
            text(doc, e, "expression", v->expression());
            simplePorts(v->role() != VarBoxItem::InVar, v->role() != VarBoxItem::OutVar);
        } else if (auto* fb = dynamic_cast<FunctionBlockItem*>(item)) {
            e.setAttribute("typeName", fb->blockType());
            if (fb->instanceName().isEmpty()) e.removeAttribute("instanceName");
            else e.setAttribute("instanceName", fb->instanceName());
            for (bool input : {true, false}) {
                const QString group = input ? "inputVariables" : "outputVariables";
                const QString cpTag = input ? "connectionPointIn" : "connectionPointOut";
                const int count = input ? fb->inputCount() : fb->outputCount();
                QSet<QString> names;
                for (int i = 0; i < count; ++i) {
                    const QString name = input ? fb->inputPortName(i) : fb->outputPortName(i);
                    names.insert(name);
                    auto cp = child(doc, port(doc, e, group, name), cpTag);
                    if (input) removeConnections(cp);
                    point(doc, cp, "relPosition", (input ? fb->inputPortPos(i) : fb->outputPortPos(i)) - item->pos());
                }
                auto list = child(doc, e, group);
                for (auto v = list.firstChildElement("variable"); !v.isNull();) {
                    auto next = v.nextSiblingElement("variable");
                    if (!names.contains(v.attribute("formalParameter"))) list.removeChild(v);
                    v = next;
                }
            }
        } else simplePorts(tag == "rightPowerRail", tag == "leftPowerRail");
    }

    for (const auto& c : m_connections) {
        if (!c.wire || c.wire->scene() != this || !elements.contains(c.dstId) || findElemById(root, c.srcId).isNull()) continue;
        auto destination = elements.value(c.dstId);
        if (destination.tagName() == "block")
            destination = port(doc, destination, "inputVariables", c.dstParam);
        auto cp = child(doc, destination, "connectionPointIn");
        auto connection = c.sourceConnection.isNull() ? doc.createElement("connection")
            : doc.importNode(c.sourceConnection, true).toElement();
        connection.setAttribute("refLocalId", c.srcId);
        if (!c.srcParam.isEmpty()) connection.setAttribute("formalParameter", c.srcParam);
        else connection.removeAttribute("formalParameter");
        // Some imported nodes are not editable/rendered yet. Keep their original
        // connection and route rather than losing it when an unrelated item changes.
        if (!elements.contains(c.srcId)) { cp.appendChild(connection); continue; }
        for (auto p = connection.firstChildElement("position"); !p.isNull();) {
            auto next = p.nextSiblingElement("position"); connection.removeChild(p); p = next;
        }
        // PLCopen stores points destination to source; QPainterPath is source to destination.
        const auto path = c.wire->path();
        for (int i = path.elementCount() - 1; i >= 0; --i) {
            auto p = doc.createElement("position");
            p.setAttribute("x", QString::number(path.elementAt(i).x / scale, 'g', 12));
            p.setAttribute("y", QString::number(path.elementAt(i).y / scale, 'g', 12));
            connection.appendChild(p);
        }
        cp.appendChild(connection);
    }
    // Unrendered targets are retained, but must not reference a deleted editable item.
    const auto connections = root.elementsByTagName("connection");
    for (int i = connections.count() - 1; i >= 0; --i) {
        auto c = connections.at(i).toElement();
        const int source = c.attribute("refLocalId").toInt();
        const auto original = findElemById(m_bodyDoc.documentElement(), source);
        if (editable.contains(original.tagName()) && findElemById(root, source).isNull()) c.parentNode().removeChild(c);
    }
    return doc;
}
