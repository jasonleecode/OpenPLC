#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QVariant>
#include <QVector>

struct SimDebugValue {
    QString name;
    QString type;
    QVariant value;
    bool forced = false;
};

class SimDebugSession : public QObject {
    Q_OBJECT

public:
    explicit SimDebugSession(QObject* parent = nullptr);

    void setTraceVariables(const QStringList& names);
    void clearTraceVariables();
    QStringList traceVariables() const;

    void handleRuntimeReply(const QJsonObject& reply);
    void requestStatusAndValues();
    void forceVariable(const QString& name, const QVariant& value);
    void releaseVariable(const QString& name);

signals:
    void commandReady(const QJsonObject& command);
    void runtimeHello(const QString& name, int version, int variableCount);
    void runtimeError(const QString& message);
    void statusUpdated(bool running, quint64 tick, int scanTimeUs, int intervalMs);
    void valuesUpdated(const QVector<SimDebugValue>& values);
    void stopped();

private:
    QVector<SimDebugValue> parseVariables(const QJsonArray& vars) const;

    QSet<QString> m_traceVariables;
    quint64 m_tick = 0;
    int m_scanTimeUs = 0;
    int m_intervalMs = 10;
    bool m_running = false;
};
