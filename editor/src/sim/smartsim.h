#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

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
    QWidget* createLogPanel();

    bool ensureSimulator();
    bool buildSimulator();
    bool generateSimVars(const QString& outDir, const QString& simVarsFile, QString* error) const;
    bool runProcessSync(const QString& program, const QStringList& args, int timeoutMs, QString* error) const;
    void sendCommand(const QJsonObject& command);
    void requestVariables();
    void handleRuntimeLine(const QByteArray& line);
    void handleRuntimeVariables(const QJsonArray& vars);
    void stopRuntime();
    void setRunState(RunState state);
    void updateStatusLabels();
    void updateDigitalLeds();
    void updateAnalogMeters();
    void appendLog(const QString& message);
    static QString ledStyle(bool on, const QString& onColor);
    static QString stateText(RunState state);

    QLabel* m_stateLed = nullptr;
    QLabel* m_stateText = nullptr;
    QLabel* m_tickText = nullptr;
    QLabel* m_scanText = nullptr;
    QLabel* m_cycleText = nullptr;
    QLabel* m_runtimeText = nullptr;
    QLabel* m_logText = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_pauseButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_stepButton = nullptr;

    QList<QLabel*> m_diLeds;
    QList<QLabel*> m_doLeds;
    QList<QProgressBar*> m_aiBars;
    QList<QProgressBar*> m_aoBars;

    QTimer* m_scanTimer = nullptr;
    QTimer* m_pollTimer = nullptr;
    QProcess* m_process = nullptr;
    ProjectModel* m_project = nullptr;
    QString m_simBinary;
    RunState m_state = RunState::Stopped;
    quint64 m_tick = 0;
    int m_scanTimeUs = 0;
    int m_cycleMs = 10;
};
