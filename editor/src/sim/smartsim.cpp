#include "smartsim.h"

#include "../core/compiler/StGenerator.h"
#include "../core/models/ProjectModel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kDigitalCount = 16;
constexpr int kAnalogCount = 4;

QLabel* makeValueLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setMinimumWidth(92);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(
        "QLabel {"
        "  border: 1px solid #b9c0ca;"
        "  border-radius: 4px;"
        "  background: #f4f6f8;"
        "  color: #1f2933;"
        "  padding: 4px 8px;"
        "  font-weight: 600;"
        "}");
    return label;
}

QLabel* makeLed()
{
    auto* led = new QLabel;
    led->setFixedSize(18, 18);
    return led;
}

QString moduleStyle()
{
    return QStringLiteral(
        "QGroupBox {"
        "  border: 1px solid #aeb7c2;"
        "  border-radius: 6px;"
        "  margin-top: 18px;"
        "  background: #eef2f6;"
        "  font-weight: 700;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 10px;"
        "  padding: 0 4px;"
        "  color: #25313f;"
        "}");
}
}

SmartSimWidget::SmartSimWidget(ProjectModel* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setObjectName("smartSimWidget");
    setMinimumSize(980, 620);
    setStyleSheet(
        "#smartSimWidget { background: #dfe5ec; }"
        "QPushButton { padding: 6px 12px; min-width: 72px; }"
        "QProgressBar {"
        "  border: 1px solid #aeb7c2;"
        "  border-radius: 4px;"
        "  background: #f4f6f8;"
        "  text-align: center;"
        "}"
        "QProgressBar::chunk { background: #2f7dba; border-radius: 3px; }");

    m_scanTimer = new QTimer(this);
    m_scanTimer->setInterval(m_cycleMs);
    connect(m_scanTimer, &QTimer::timeout, this, [this] {
        requestVariables();
    });

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(250);
    connect(m_pollTimer, &QTimer::timeout, this, &SmartSimWidget::requestVariables);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    root->addWidget(createStatusPanel());

    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    body->addWidget(createRackPanel(), 1);
    body->addWidget(createLogPanel());
    root->addLayout(body, 1);

    setRunState(RunState::Stopped);
    updateDigitalLeds();
    updateAnalogMeters();
    appendLog("SmartSim panel ready.");
}

SmartSimWidget::~SmartSimWidget()
{
    stopRuntime();
}

QWidget* SmartSimWidget::createStatusPanel()
{
    auto* frame = new QFrame;
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(
        "QFrame {"
        "  background: #f7f9fb;"
        "  border: 1px solid #b9c0ca;"
        "  border-radius: 6px;"
        "}");

    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(12);

    m_stateLed = makeLed();
    m_stateText = makeValueLabel("STOPPED");
    m_tickText = makeValueLabel("Tick 0");
    m_scanText = makeValueLabel("Scan 0 us");
    m_cycleText = makeValueLabel(QString("Cycle %1 ms").arg(m_cycleMs));
    m_runtimeText = makeValueLabel("PC SmartSim");

    layout->addWidget(new QLabel("PLC"));
    layout->addWidget(m_stateLed);
    layout->addWidget(m_stateText);
    layout->addSpacing(10);
    layout->addWidget(m_tickText);
    layout->addWidget(m_scanText);
    layout->addWidget(m_cycleText);
    layout->addWidget(m_runtimeText);
    layout->addStretch();

    m_startButton = new QPushButton("Run");
    m_pauseButton = new QPushButton("Pause");
    m_stopButton = new QPushButton("Stop");
    m_stepButton = new QPushButton("Step");
    layout->addWidget(m_startButton);
    layout->addWidget(m_pauseButton);
    layout->addWidget(m_stopButton);
    layout->addWidget(m_stepButton);

    connect(m_startButton, &QPushButton::clicked, this, [this] {
        if (!ensureSimulator())
            return;
        sendCommand(QJsonObject{{"cmd", "start"}, {"intervalMs", m_cycleMs}});
        setRunState(RunState::Running);
        m_pollTimer->start();
        appendLog("PLC runtime started.");
    });
    connect(m_pauseButton, &QPushButton::clicked, this, [this] {
        sendCommand(QJsonObject{{"cmd", "pause"}});
        setRunState(RunState::Paused);
        appendLog("PLC runtime paused.");
    });
    connect(m_stopButton, &QPushButton::clicked, this, [this] {
        stopRuntime();
        setRunState(RunState::Stopped);
        m_tick = 0;
        m_scanTimeUs = 0;
        updateStatusLabels();
        appendLog("PLC runtime stopped.");
    });
    connect(m_stepButton, &QPushButton::clicked, this, [this] {
        if (!ensureSimulator())
            return;
        sendCommand(QJsonObject{{"cmd", "step"}});
        sendCommand(QJsonObject{{"cmd", "readVars"}});
        setRunState(RunState::Paused);
        appendLog("Single scan executed.");
    });

    return frame;
}

QWidget* SmartSimWidget::createRackPanel()
{
    auto* frame = new QFrame;
    frame->setStyleSheet(
        "QFrame {"
        "  background: #c8d0da;"
        "  border: 1px solid #98a4b1;"
        "  border-radius: 6px;"
        "}");

    auto* layout = new QGridLayout(frame);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(12);

    auto* cpu = new QGroupBox("CPU");
    cpu->setStyleSheet(moduleStyle());
    auto* cpuLayout = new QVBoxLayout(cpu);
    cpuLayout->setSpacing(10);

    auto* cpuTitle = new QLabel("TiZi PLC");
    cpuTitle->setAlignment(Qt::AlignCenter);
    cpuTitle->setStyleSheet(
        "QLabel {"
        "  background: #25313f;"
        "  color: white;"
        "  border-radius: 4px;"
        "  padding: 10px;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "}");
    cpuLayout->addWidget(cpuTitle);
    cpuLayout->addWidget(new QLabel("Mode: PC simulation"));
    cpuLayout->addWidget(new QLabel("Runtime: SmartSim MVP"));
    cpuLayout->addWidget(new QLabel("Backplane: local process"));
    cpuLayout->addStretch();

    layout->addWidget(cpu, 0, 0, 2, 1);
    layout->addWidget(createDigitalPanel("DI 16x24VDC", false), 0, 1);
    layout->addWidget(createDigitalPanel("DO 16xRelay", true), 0, 2);
    layout->addWidget(createAnalogPanel("AI 4x0-10V", false), 1, 1);
    layout->addWidget(createAnalogPanel("AO 4x0-10V", true), 1, 2);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 2);
    layout->setColumnStretch(2, 2);
    layout->setRowStretch(0, 1);
    layout->setRowStretch(1, 1);

    return frame;
}

QWidget* SmartSimWidget::createDigitalPanel(const QString& title, bool output)
{
    auto* group = new QGroupBox(title);
    group->setStyleSheet(moduleStyle());
    auto* grid = new QGridLayout(group);
    grid->setContentsMargins(12, 16, 12, 12);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    QList<QLabel*>& leds = output ? m_doLeds : m_diLeds;
    for (int i = 0; i < kDigitalCount; ++i) {
        auto* name = new QLabel(QString("%1%2").arg(output ? "Q" : "I").arg(i, 2, 10, QLatin1Char('0')));
        name->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* led = makeLed();
        leds.append(led);

        const int row = i % 8;
        const int col = (i / 8) * 2;
        grid->addWidget(name, row, col);
        grid->addWidget(led, row, col + 1);
    }
    return group;
}

QWidget* SmartSimWidget::createAnalogPanel(const QString& title, bool output)
{
    auto* group = new QGroupBox(title);
    group->setStyleSheet(moduleStyle());
    auto* layout = new QGridLayout(group);
    layout->setContentsMargins(12, 16, 12, 12);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(10);

    QList<QProgressBar*>& bars = output ? m_aoBars : m_aiBars;
    for (int i = 0; i < kAnalogCount; ++i) {
        auto* name = new QLabel(QString("%1%2").arg(output ? "AQ" : "AI").arg(i));
        auto* bar = new QProgressBar;
        bar->setRange(0, 10000);
        bar->setFormat("%v mV");
        bar->setMinimumWidth(180);
        bars.append(bar);
        layout->addWidget(name, i, 0);
        layout->addWidget(bar, i, 1);
    }
    return group;
}

QWidget* SmartSimWidget::createLogPanel()
{
    auto* frame = new QFrame;
    frame->setMinimumWidth(260);
    frame->setStyleSheet(
        "QFrame {"
        "  background: #f7f9fb;"
        "  border: 1px solid #b9c0ca;"
        "  border-radius: 6px;"
        "}");

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* title = new QLabel("Simulation Events");
    title->setStyleSheet("font-weight: 700; color: #25313f;");
    m_logText = new QLabel;
    m_logText->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_logText->setWordWrap(true);
    m_logText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_logText->setStyleSheet(
        "QLabel {"
        "  background: #111827;"
        "  color: #d1d5db;"
        "  border-radius: 4px;"
        "  padding: 10px;"
        "  font-family: monospace;"
        "}");

    layout->addWidget(title);
    layout->addWidget(m_logText, 1);
    return frame;
}

bool SmartSimWidget::ensureSimulator()
{
    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
            while (m_process->canReadLine())
                handleRuntimeLine(m_process->readLine().trimmed());
        });
        connect(m_process, &QProcess::readyReadStandardError, this, [this] {
            const QString err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
            if (!err.isEmpty())
                appendLog("runtime stderr: " + err);
        });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            appendLog("runtime process error: " + m_process->errorString());
        });
        connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
            appendLog(QString("runtime exited with code %1.").arg(exitCode));
            m_pollTimer->stop();
            m_scanTimer->stop();
            setRunState(RunState::Stopped);
        });
    }

    if (m_process->state() == QProcess::Running)
        return true;

    if (m_simBinary.isEmpty() || !QFileInfo::exists(m_simBinary)) {
        if (!buildSimulator())
            return false;
    }

    m_process->start(m_simBinary);
    if (!m_process->waitForStarted(5000)) {
        appendLog("cannot start simulator: " + m_process->errorString());
        return false;
    }

    appendLog("runtime process launched.");
    sendCommand(QJsonObject{{"cmd", "hello"}});
    sendCommand(QJsonObject{{"cmd", "init"}});
    sendCommand(QJsonObject{{"cmd", "readVars"}});
    return true;
}

bool SmartSimWidget::buildSimulator()
{
    if (!m_project || m_project->filePath.isEmpty()) {
        appendLog("project must be saved before simulation.");
        return false;
    }

    QFile xmlFile(m_project->filePath);
    if (!xmlFile.open(QFile::ReadOnly | QFile::Text)) {
        appendLog("cannot read project file.");
        return false;
    }

    const QString stCode = StGenerator::fromXml(QString::fromUtf8(xmlFile.readAll()));
    if (stCode.isEmpty()) {
        appendLog("ST generation failed: " + StGenerator::lastError());
        return false;
    }

    QString safeName;
    for (QChar c : m_project->projectName)
        safeName += c.isLetterOrNumber() ? c : QChar('_');
    if (safeName.isEmpty())
        safeName = "project";

    const QString buildDir = QFileInfo(m_project->filePath).absolutePath() + "/.tizi_sim/" + safeName;
    const QString outDir = buildDir + "/iec2c";
    QDir().mkpath(outDir);

    const QString stFile = buildDir + "/project.st";
    QFile stOut(stFile);
    if (!stOut.open(QFile::WriteOnly | QFile::Text | QFile::Truncate)) {
        appendLog("cannot write simulation ST file.");
        return false;
    }
    stOut.write(stCode.toUtf8());
    stOut.close();

    const QString matiecDir = QString(MATIEC_DIR);
    const QString simRuntimeDir = QString(SIM_RUNTIME_DIR);
    if (!QFileInfo(matiecDir + "/iec2c").isExecutable()) {
        appendLog("matiec iec2c not found: " + matiecDir);
        return false;
    }
    if (!QFileInfo(simRuntimeDir + "/sim_main.c").exists()) {
        appendLog("sim runtime not found: " + simRuntimeDir);
        return false;
    }

    appendLog("building simulation runtime...");

    QString error;
    if (!runProcessSync(matiecDir + "/iec2c",
                        {"-p", "-i", "-I", matiecDir + "/lib", "-T", outDir, stFile},
                        30000,
                        &error)) {
        appendLog("iec2c failed: " + error);
        return false;
    }

    const QString simVarsFile = buildDir + "/sim_vars.c";
    if (!generateSimVars(outDir, simVarsFile, &error)) {
        appendLog("sim var generation failed: " + error);
        return false;
    }

    QStringList iecSources = {outDir + "/config.c"};
    for (const QFileInfo& fi : QDir(outDir).entryInfoList({"resource*.c"}, QDir::Files))
        iecSources << fi.absoluteFilePath();

    m_simBinary = buildDir + "/smart_sim_program";
    QStringList ccArgs = {
        "-w",
        "-I", simRuntimeDir,
        "-I", matiecDir + "/lib/C",
        "-I", outDir,
        simRuntimeDir + "/sim_main.c",
        simVarsFile,
    };
    ccArgs << iecSources;
    ccArgs << "-o" << m_simBinary << "-lm";

    if (!runProcessSync("gcc", ccArgs, 60000, &error)) {
        appendLog("gcc failed: " + error);
        return false;
    }

    appendLog("simulation binary ready.");
    return true;
}

bool SmartSimWidget::generateSimVars(const QString& outDir, const QString& simVarsFile, QString* error) const
{
    QFile pousFile(outDir + "/POUS.h");
    QFile resourceFile(outDir + "/resource1.c");
    if (!pousFile.open(QFile::ReadOnly | QFile::Text)
        || !resourceFile.open(QFile::ReadOnly | QFile::Text)) {
        if (error) *error = "cannot read POUS.h or resource1.c";
        return false;
    }

    const QString pous = QString::fromUtf8(pousFile.readAll());
    const QString resource = QString::fromUtf8(resourceFile.readAll());

    QRegularExpression instanceRe("\\b([A-Z][A-Z0-9_]*)\\s+(RESOURCE1__[A-Z0-9_]+);");
    QRegularExpressionMatch instanceMatch = instanceRe.match(resource);
    if (!instanceMatch.hasMatch()) {
        if (error) *error = "cannot find RESOURCE1 program instance";
        return false;
    }

    const QString programType = instanceMatch.captured(1);
    const QString instanceName = instanceMatch.captured(2);
    QRegularExpression structRe(
        "typedef\\s+struct\\s*\\{([\\s\\S]*?)\\}\\s*" + QRegularExpression::escape(programType) + "\\s*;");
    QRegularExpressionMatch structMatch = structRe.match(pous);
    if (!structMatch.hasMatch()) {
        if (error) *error = "cannot find program struct";
        return false;
    }

    const QMap<QString, QString> supported = {
        {"BOOL", "SIM_VAR_BOOL"},
        {"INT", "SIM_VAR_INT"},
        {"DINT", "SIM_VAR_DINT"},
        {"REAL", "SIM_VAR_REAL"},
        {"LREAL", "SIM_VAR_LREAL"},
    };

    QStringList entries;
    QRegularExpression varRe("__DECLARE_VAR\\(([^,]+),([^)]+)\\)");
    QRegularExpressionMatchIterator it = varRe.globalMatch(structMatch.captured(1));
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString iecType = m.captured(1).trimmed();
        const QString name = m.captured(2).trimmed();
        const QString simType = supported.value(iecType);
        if (!simType.isEmpty()) {
            entries << QString("    {\"main.%1\", %2, &%3.%4.value, 0, 0.0},")
                .arg(name, simType, instanceName, name);
        }
    }

    if (entries.isEmpty()) {
        if (error) *error = "no supported variables found";
        return false;
    }

    QFile out(simVarsFile);
    if (!out.open(QFile::WriteOnly | QFile::Text | QFile::Truncate)) {
        if (error) *error = "cannot write sim_vars.c";
        return false;
    }

    QString code;
    code += "#include \"sim_api.h\"\n";
    code += "#include \"iec_std_lib.h\"\n";
    code += "#include \"POUS.h\"\n\n";
    code += QString("extern %1 %2;\n\n").arg(programType, instanceName);
    code += "SimVar sim_vars[] = {\n";
    code += entries.join('\n');
    code += "\n};\n";
    code += "const size_t sim_var_count = sizeof(sim_vars) / sizeof(sim_vars[0]);\n";
    out.write(code.toUtf8());
    return true;
}

bool SmartSimWidget::runProcessSync(const QString& program,
                                    const QStringList& args,
                                    int timeoutMs,
                                    QString* error) const
{
    QProcess proc;
    proc.start(program, args);
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(1000);
        if (error) *error = "process timed out";
        return false;
    }

    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    if (proc.exitCode() != 0) {
        if (error) *error = err.isEmpty() ? out : err;
        return false;
    }
    if (error) *error = err;
    return true;
}

void SmartSimWidget::sendCommand(const QJsonObject& command)
{
    if (!m_process || m_process->state() != QProcess::Running)
        return;

    const QByteArray line = QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n';
    m_process->write(line);
}

void SmartSimWidget::requestVariables()
{
    if (!m_process || m_process->state() != QProcess::Running)
        return;
    sendCommand(QJsonObject{{"cmd", "status"}});
    sendCommand(QJsonObject{{"cmd", "readVars"}});
}

void SmartSimWidget::handleRuntimeLine(const QByteArray& line)
{
    if (line.isEmpty())
        return;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        appendLog("invalid runtime reply.");
        return;
    }

    const QJsonObject obj = doc.object();
    if (!obj.value("ok").toBool(false)) {
        appendLog("runtime error: " + obj.value("error").toString("unknown"));
        return;
    }

    if (obj.contains("name"))
        appendLog(obj.value("name").toString("SmartSim") + " connected.");

    if (obj.contains("tick"))
        m_tick = static_cast<quint64>(obj.value("tick").toDouble());
    if (obj.contains("scanTimeUs"))
        m_scanTimeUs = obj.value("scanTimeUs").toInt();
    if (obj.contains("intervalMs"))
        m_cycleMs = obj.value("intervalMs").toInt(m_cycleMs);
    if (obj.contains("running"))
        m_state = obj.value("running").toBool() ? RunState::Running : m_state;
    if (obj.contains("vars"))
        handleRuntimeVariables(obj.value("vars").toArray());
    if (obj.value("stopped").toBool(false))
        setRunState(RunState::Stopped);

    updateStatusLabels();
}

void SmartSimWidget::handleRuntimeVariables(const QJsonArray& vars)
{
    int di = 0;
    int dout = 0;
    int ai = 0;
    int ao = 0;

    for (const QJsonValue& value : vars) {
        const QJsonObject var = value.toObject();
        const QString name = var.value("name").toString();
        const QString type = var.value("type").toString();
        const QString lower = name.toLower();

        if (type == "BOOL") {
            const bool on = var.value("value").toBool();
            const bool isOutput = lower.contains(".q") || lower.contains("do")
                || lower.contains("out") || lower.contains("done");
            QList<QLabel*>& leds = isOutput ? m_doLeds : m_diLeds;
            int& index = isOutput ? dout : di;
            if (index < leds.size())
                leds[index++]->setStyleSheet(ledStyle(on, isOutput ? "#38bdf8" : "#22c55e"));
            continue;
        }

        const bool isAnalogType = type == "INT" || type == "DINT" || type == "REAL" || type == "LREAL";
        if (!isAnalogType)
            continue;

        double raw = var.value("value").toDouble();
        int scaled = 0;
        if (raw >= 0.0 && raw <= 10.0)
            scaled = static_cast<int>(raw * 1000.0);
        else
            scaled = static_cast<int>(raw);
        scaled = qBound(0, scaled, 10000);

        const bool isOutput = lower.contains("ao") || lower.contains("aq") || lower.contains(".q");
        QList<QProgressBar*>& bars = isOutput ? m_aoBars : m_aiBars;
        int& index = isOutput ? ao : ai;
        if (index < bars.size())
            bars[index++]->setValue(scaled);
    }

    for (; di < m_diLeds.size(); ++di)
        m_diLeds[di]->setStyleSheet(ledStyle(false, "#22c55e"));
    for (; dout < m_doLeds.size(); ++dout)
        m_doLeds[dout]->setStyleSheet(ledStyle(false, "#38bdf8"));
    for (; ai < m_aiBars.size(); ++ai)
        m_aiBars[ai]->setValue(0);
    for (; ao < m_aoBars.size(); ++ao)
        m_aoBars[ao]->setValue(0);
}

void SmartSimWidget::stopRuntime()
{
    m_scanTimer->stop();
    m_pollTimer->stop();
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;

    sendCommand(QJsonObject{{"cmd", "stop"}});
    if (!m_process->waitForFinished(1000)) {
        m_process->terminate();
        if (!m_process->waitForFinished(1000))
            m_process->kill();
    }
}

void SmartSimWidget::setRunState(RunState state)
{
    m_state = state;
    m_scanTimer->stop();

    m_startButton->setEnabled(m_state != RunState::Running);
    m_pauseButton->setEnabled(m_state == RunState::Running);
    m_stopButton->setEnabled(m_state != RunState::Stopped);
    m_stepButton->setEnabled(m_state != RunState::Running);
    updateStatusLabels();
}

void SmartSimWidget::updateStatusLabels()
{
    const QString color = m_state == RunState::Running ? "#22c55e"
        : m_state == RunState::Paused ? "#f59e0b"
        : "#6b7280";
    m_stateLed->setStyleSheet(ledStyle(m_state != RunState::Stopped, color));
    m_stateText->setText(stateText(m_state).toUpper());
    m_tickText->setText(QString("Tick %1").arg(m_tick));
    m_scanText->setText(QString("Scan %1 us").arg(m_scanTimeUs));
    m_cycleText->setText(QString("Cycle %1 ms").arg(m_cycleMs));
}

void SmartSimWidget::updateDigitalLeds()
{
    for (int i = 0; i < m_diLeds.size(); ++i) {
        const bool on = ((m_tick + static_cast<quint64>(i)) % 5U) == 0U
            || QRandomGenerator::global()->bounded(100) < 8;
        m_diLeds[i]->setStyleSheet(ledStyle(on, "#22c55e"));
    }

    for (int i = 0; i < m_doLeds.size(); ++i) {
        const bool on = ((m_tick / 2U + static_cast<quint64>(i)) % 7U) == 0U;
        m_doLeds[i]->setStyleSheet(ledStyle(on, "#38bdf8"));
    }
}

void SmartSimWidget::updateAnalogMeters()
{
    for (int i = 0; i < m_aiBars.size(); ++i) {
        const int value = 1200 + static_cast<int>((m_tick * (170 + i * 50)) % 8200U);
        m_aiBars[i]->setValue(value);
    }

    for (int i = 0; i < m_aoBars.size(); ++i) {
        const int value = 800 + static_cast<int>((m_tick * (230 + i * 30)) % 8800U);
        m_aoBars[i]->setValue(value);
    }
}

void SmartSimWidget::appendLog(const QString& message)
{
    const QString line = QString("[%1] %2")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
        .arg(message);

    QStringList lines = m_logText->text().split('\n', Qt::SkipEmptyParts);
    lines.prepend(line);
    while (lines.size() > 12)
        lines.removeLast();
    m_logText->setText(lines.join('\n'));
}

QString SmartSimWidget::ledStyle(bool on, const QString& onColor)
{
    const QString fill = on ? onColor : QStringLiteral("#4b5563");
    const QString border = on ? QStringLiteral("#f8fafc") : QStringLiteral("#1f2937");
    return QString(
        "QLabel {"
        "  border: 2px solid %1;"
        "  border-radius: 9px;"
        "  background: %2;"
        "}").arg(border, fill);
}

QString SmartSimWidget::stateText(RunState state)
{
    switch (state) {
    case RunState::Stopped: return "Stopped";
    case RunState::Running: return "Running";
    case RunState::Paused: return "Paused";
    }
    return "Unknown";
}
