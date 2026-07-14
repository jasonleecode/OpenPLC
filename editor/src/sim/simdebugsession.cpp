#include "simdebugsession.h"

SimDebugSession::SimDebugSession(QObject* parent)
    : QObject(parent)
{
}

void SimDebugSession::setTraceVariables(const QStringList& names)
{
    m_traceVariables = QSet<QString>(names.cbegin(), names.cend());
    QJsonArray traceNames;
    for (const QString& name : names)
        traceNames.append(name);
    emit commandReady(QJsonObject{{"cmd", "setTraceVariables"}, {"names", traceNames}});
    emit commandReady(QJsonObject{{"cmd", "traceData"}});
}

void SimDebugSession::clearTraceVariables()
{
    m_traceVariables.clear();
    emit commandReady(QJsonObject{{"cmd", "setTraceVariables"}, {"names", QJsonArray{}}});
    emit commandReady(QJsonObject{{"cmd", "readVars"}});
}

QStringList SimDebugSession::traceVariables() const
{
    QStringList names = m_traceVariables.values();
    names.sort();
    return names;
}

void SimDebugSession::handleRuntimeReply(const QJsonObject& reply)
{
    if (!reply.value("ok").toBool(false)) {
        emit runtimeError(reply.value("error").toString("unknown runtime error"));
        return;
    }

    if (reply.contains("name")) {
        emit runtimeHello(reply.value("name").toString("SmartSim"),
                          reply.value("version").toInt(),
                          reply.value("varCount").toInt());
    }

    if (reply.contains("tick"))
        m_tick = static_cast<quint64>(reply.value("tick").toDouble());
    if (reply.contains("scanTimeUs"))
        m_scanTimeUs = reply.value("scanTimeUs").toInt();
    if (reply.contains("intervalMs"))
        m_intervalMs = reply.value("intervalMs").toInt(m_intervalMs);
    if (reply.contains("running"))
        m_running = reply.value("running").toBool();
    if (reply.contains("tick") || reply.contains("scanTimeUs")
        || reply.contains("intervalMs") || reply.contains("running")) {
        emit statusUpdated(m_running, m_tick, m_scanTimeUs, m_intervalMs);
    }

    if (reply.contains("vars"))
        emit valuesUpdated(parseVariables(reply.value("vars").toArray()));

    if (reply.value("stopped").toBool(false))
        emit stopped();
}

void SimDebugSession::requestStatusAndValues()
{
    emit commandReady(QJsonObject{{"cmd", "status"}});
    emit commandReady(QJsonObject{{"cmd", m_traceVariables.isEmpty() ? "readVars" : "traceData"}});
}

void SimDebugSession::forceVariable(const QString& name, const QVariant& value)
{
    emit commandReady(QJsonObject{
        {"cmd", "forceVar"},
        {"name", name},
        {"value", QJsonValue::fromVariant(value)}
    });
}

void SimDebugSession::releaseVariable(const QString& name)
{
    emit commandReady(QJsonObject{
        {"cmd", "releaseForce"},
        {"name", name}
    });
}

QVector<SimDebugValue> SimDebugSession::parseVariables(const QJsonArray& vars) const
{
    QVector<SimDebugValue> values;
    values.reserve(vars.size());

    for (const QJsonValue& value : vars) {
        const QJsonObject var = value.toObject();
        const QString name = var.value("name").toString();
        if (!m_traceVariables.isEmpty() && !m_traceVariables.contains(name))
            continue;

        SimDebugValue parsed;
        parsed.name = name;
        parsed.type = var.value("type").toString();
        parsed.value = var.value("value").toVariant();
        parsed.forced = var.value("forced").toBool(false);
        values.append(parsed);
    }

    return values;
}
