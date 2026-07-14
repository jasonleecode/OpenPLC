#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QVariant>
#include <QVector>
#include <QWidget>

#include "simdebugsession.h"

class QLabel;
class ProjectModel;
class QProcess;
class QPushButton;
class QProgressBar;
class QTimer;

class SmartSimWidget : public QWidget {
    Q_OBJECT

public:
    explicit SmartSimWidget(ProjectModel* project, QWidget* parent = nullptr);
    ~SmartSimWidget() override;

public slots:
    bool downloadProgram();
    bool runProgram();
    void stopProgram();
    void forceVariable(const QString& name, const QVariant& value);
    void releaseVariable(const QString& name);
    void setIoMappings(const QStringList& diVars,
                       const QStringList& doVars,
                       const QStringList& aiVars,
                       const QStringList& aoVars);

signals:
    void debugValuesChanged(const QVector<SimDebugValue>& vars);
    void simulationEvent(const QString& message);
    void simulationConnectionChanged(bool connected);
    void simulationRunStateChanged(const QString& state);

private:
    enum class RunState {
        Stopped,
        Running,
        Paused
    };

    QWidget* createStatusPanel();
    QWidget* createRackPanel();
    QWidget* createDigitalPanel(const QString& title, bool output);
    QWidget* createAnalogPanel(const QString& title, bool output);

    bool ensureSimulator();
    bool startRuntimeProcess();
    bool buildSimulator();
    bool generateSimVars(const QString& outDir, const QString& simVarsFile, QString* error) const;
    bool runProcessSync(const QString& program, const QStringList& args, int timeoutMs, QString* error) const;
    void sendCommand(const QJsonObject& command);
    void requestVariables();
    void handleRuntimeLine(const QByteArray& line);
    void handleRuntimeVariables(const QVector<SimDebugValue>& vars);
    void stopRuntime();
    void setRunState(RunState state);
    void updateStatusLabels();
    void updateDigitalLeds();
    void updateAnalogMeters();
    void appendLog(const QString& message);
    bool eventFilter(QObject* watched, QEvent* event) override;
    void toggleDigitalInput(int index);
    static QString ledStyle(bool on, const QString& onColor);
    static QString stateText(RunState state);

    QLabel* m_stateLed = nullptr;
    QLabel* m_stateText = nullptr;
    QLabel* m_tickText = nullptr;
    QLabel* m_scanText = nullptr;
    QLabel* m_cycleText = nullptr;
    QLabel* m_runtimeText = nullptr;
    QLabel* m_programText = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_pauseButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_stepButton = nullptr;

    QList<QLabel*> m_diLeds;
    QList<QLabel*> m_doLeds;
    QList<QProgressBar*> m_aiBars;
    QList<QProgressBar*> m_aoBars;
    QStringList m_diVars;
    QStringList m_doVars;
    QStringList m_aiVars;
    QStringList m_aoVars;
    QMap<QString, SimDebugValue> m_lastValues;

    QTimer* m_scanTimer = nullptr;
    QTimer* m_pollTimer = nullptr;
    QProcess* m_process = nullptr;
    SimDebugSession* m_debugSession = nullptr;
    ProjectModel* m_project = nullptr;
    QString m_simBinary;
    bool m_programLoaded = false;
    RunState m_state = RunState::Stopped;
    quint64 m_tick = 0;
    int m_scanTimeUs = 0;
    int m_cycleMs = 10;
};
