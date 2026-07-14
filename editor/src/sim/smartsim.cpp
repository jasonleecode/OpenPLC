#include "smartsim.h"

#include "simdebugsession.h"

#include "../core/compiler/StGenerator.h"
#include "../core/models/ProjectModel.h"

#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMap>
#include <QMouseEvent>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace {
constexpr int kDigitalCount = 16;
constexpr int kAnalogCount = 4;

QLabel* makeValueLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setMinimumWidth(78);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(
        "QLabel {"
        "  border: 1px solid #b9c0ca;"
        "  border-radius: 4px;"
        "  background: #f4f6f8;"
        "  color: #1f2933;"
        "  padding: 3px 6px;"
        "  font-weight: 600;"
        "}");
    return label;
}

QLabel* makeLed()
{
    auto* led = new QLabel;
    led->setFixedSize(14, 14);
    return led;
}

QString moduleStyle()
{
    return QStringLiteral(
        "QGroupBox {"
        "  border: 1px solid #aeb7c2;"
        "  border-radius: 6px;"
        "  margin-top: 14px;"
        "  background: #eef2f6;"
        "  font-weight: 700;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 8px;"
        "  padding: 0 3px;"
        "  color: #25313f;"
        "}");
}
}

SmartSimWidget::SmartSimWidget(ProjectModel* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setObjectName("smartSimWidget");
    setMinimumSize(760, 500);
    setStyleSheet(
        "#smartSimWidget { background: #dfe5ec; }"
        "QPushButton { padding: 5px 9px; min-width: 56px; }"
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

    m_debugSession = new SimDebugSession(this);
    connect(m_debugSession, &SimDebugSession::commandReady,
            this, &SmartSimWidget::sendCommand);
    connect(m_debugSession, &SimDebugSession::runtimeHello, this,
            [this](const QString& name, int, int) {
        appendLog(name + " connected.");
        emit simulationConnectionChanged(true);
    });
    connect(m_debugSession, &SimDebugSession::runtimeError, this,
            [this](const QString& message) {
        appendLog("runtime error: " + message);
    });
    connect(m_debugSession, &SimDebugSession::statusUpdated, this,
            [this](bool running, quint64 tick, int scanTimeUs, int intervalMs) {
        m_tick = tick;
        m_scanTimeUs = scanTimeUs;
        m_cycleMs = intervalMs;
        m_state = running ? RunState::Running : m_state;
        updateStatusLabels();
    });
    connect(m_debugSession, &SimDebugSession::valuesUpdated,
            this, &SmartSimWidget::handleRuntimeVariables);
    connect(m_debugSession, &SimDebugSession::stopped, this,
            [this] { setRunState(RunState::Stopped); });

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    root->addWidget(createStatusPanel());

    auto* body = new QHBoxLayout;
    body->setSpacing(8);
    body->addWidget(createRackPanel(), 1);
    root->addLayout(body, 1);

    setRunState(RunState::Stopped);
    updateDigitalLeds();
    updateAnalogMeters();
    appendLog("SmartSim panel ready.");
}

SmartSimWidget::~SmartSimWidget()
{
    stopRuntime();
    emit simulationConnectionChanged(false);
}

bool SmartSimWidget::downloadProgram()
{
    if (!m_project || m_project->filePath.isEmpty()) {
        appendLog("project must be saved before downloading to SmartSim.");
        return false;
    }

    stopRuntime();
    m_programLoaded = false;
    m_simBinary.clear();
    if (m_programText)
        m_programText->setText("Program: <none>");
    setRunState(RunState::Stopped);
    m_tick = 0;
    m_scanTimeUs = 0;
    updateStatusLabels();

    appendLog("downloading project to SmartSim...");
    if (!buildSimulator())
        return false;

    m_programLoaded = true;
    if (m_programText) {
        QString programName = m_project->projectName.trimmed();
        if (programName.isEmpty())
            programName = QFileInfo(m_project->filePath).completeBaseName();
        m_programText->setText("Program: " + programName);
    }
    appendLog("program downloaded to SmartSim.");
    if (!startRuntimeProcess())
        return false;

    setRunState(RunState::Stopped);
    requestVariables();
    return true;
}

bool SmartSimWidget::runProgram()
{
    if (!ensureSimulator())
        return false;

    sendCommand(QJsonObject{{"cmd", "start"}, {"intervalMs", m_cycleMs}});
    setRunState(RunState::Running);
    m_pollTimer->start();
    appendLog("PLC runtime started.");
    return true;
}

void SmartSimWidget::stopProgram()
{
    stopRuntime();
    setRunState(RunState::Stopped);
    m_tick = 0;
    m_scanTimeUs = 0;
    updateStatusLabels();
    appendLog("PLC runtime stopped.");
}

void SmartSimWidget::forceVariable(const QString& name, const QVariant& value)
{
    if (!m_debugSession || name.isEmpty())
        return;

    m_debugSession->forceVariable(name, value);
    appendLog(QString("forced %1.").arg(name));
    requestVariables();
}

void SmartSimWidget::releaseVariable(const QString& name)
{
    if (!m_debugSession || name.isEmpty())
        return;

    m_debugSession->releaseVariable(name);
    appendLog(QString("released force on %1.").arg(name));
    requestVariables();
}

void SmartSimWidget::setIoMappings(const QStringList& diVars,
                                   const QStringList& doVars,
                                   const QStringList& aiVars,
                                   const QStringList& aoVars)
{
    auto normalized = [](QStringList vars, int size) {
        while (vars.size() < size)
            vars.append(QString());
        while (vars.size() > size)
            vars.removeLast();
        return vars;
    };

    m_diVars = normalized(diVars, m_diLeds.size());
    m_doVars = normalized(doVars, m_doLeds.size());
    m_aiVars = normalized(aiVars, m_aiBars.size());
    m_aoVars = normalized(aoVars, m_aoBars.size());
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
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(7);

    m_stateLed = makeLed();
    m_stateText = makeValueLabel("STOPPED");
    m_tickText = makeValueLabel("Tick 0");
    m_scanText = makeValueLabel("Scan 0 us");
    m_cycleText = makeValueLabel(QString("Cycle %1 ms").arg(m_cycleMs));
    m_runtimeText = makeValueLabel("PC SmartSim");

    layout->addWidget(new QLabel("PLC"));
    layout->addWidget(m_stateLed);
    layout->addWidget(m_stateText);
    layout->addSpacing(4);
    layout->addWidget(m_tickText);
    layout->addWidget(m_scanText);
    layout->addWidget(m_cycleText);
    layout->addWidget(m_runtimeText);
    layout->addStretch();

    m_startButton = new QPushButton("Run");
    m_pauseButton = new QPushButton("Pause");
    m_stopButton = new QPushButton("Stop");
    m_stepButton = new QPushButton("Step");
    for (QPushButton* button : {m_startButton, m_pauseButton, m_stopButton, m_stepButton}) {
        button->setFixedWidth(36);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    layout->addWidget(m_startButton);
    layout->addWidget(m_pauseButton);
    layout->addWidget(m_stopButton);
    layout->addWidget(m_stepButton);

    connect(m_startButton, &QPushButton::clicked, this, &SmartSimWidget::runProgram);
    connect(m_pauseButton, &QPushButton::clicked, this, [this] {
        sendCommand(QJsonObject{{"cmd", "pause"}});
        setRunState(RunState::Paused);
        appendLog("PLC runtime paused.");
    });
    connect(m_stopButton, &QPushButton::clicked, this, &SmartSimWidget::stopProgram);
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
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(8);

    auto* cpu = new QGroupBox("CPU");
    cpu->setStyleSheet(moduleStyle());
    auto* cpuLayout = new QVBoxLayout(cpu);
    cpuLayout->setContentsMargins(8, 12, 8, 8);
    cpuLayout->setSpacing(6);

    auto* cpuTitle = new QLabel("TiZi PLC");
    cpuTitle->setAlignment(Qt::AlignCenter);
    cpuTitle->setStyleSheet(
        "QLabel {"
        "  background: #25313f;"
        "  color: white;"
        "  border-radius: 4px;"
        "  padding: 7px;"
        "  font-size: 15px;"
        "  font-weight: 700;"
        "}");
    cpuLayout->addWidget(cpuTitle);
    cpuLayout->addWidget(new QLabel("Mode: PC simulation"));
    cpuLayout->addWidget(new QLabel("Runtime: SmartSim MVP"));
    m_programText = new QLabel("Program: <none>");
    m_programText->setWordWrap(true);
    cpuLayout->addWidget(m_programText);
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
    grid->setContentsMargins(8, 12, 8, 8);
    grid->setHorizontalSpacing(5);
    grid->setVerticalSpacing(5);

    QList<QLabel*>& leds = output ? m_doLeds : m_diLeds;
    for (int i = 0; i < kDigitalCount; ++i) {
        auto* name = new QLabel(QString("%1%2").arg(output ? "Q" : "I").arg(i));
        name->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        name->setMinimumWidth(16);
        auto* led = makeLed();
        if (!output) {
            const QString tooltip = QString("Click to toggle DI%1 force value").arg(i);
            name->setCursor(Qt::PointingHandCursor);
            led->setCursor(Qt::PointingHandCursor);
            name->setToolTip(tooltip);
            led->setToolTip(tooltip);
            name->setProperty("simDiIndex", i);
            led->setProperty("simDiIndex", i);
            name->installEventFilter(this);
            led->installEventFilter(this);
        }
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
    layout->setContentsMargins(8, 12, 8, 8);
    layout->setHorizontalSpacing(5);
    layout->setVerticalSpacing(5);

    QList<QProgressBar*>& bars = output ? m_aoBars : m_aiBars;
    for (int i = 0; i < kAnalogCount; ++i) {
        auto* name = new QLabel(QString("%1%2").arg(output ? "AQ" : "AI").arg(i));
        auto* bar = new QProgressBar;
        bar->setRange(0, 10000);
        bar->setFormat("%v mV");
        bar->setMinimumWidth(120);
        bars.append(bar);
        layout->addWidget(name, i, 0);
        layout->addWidget(bar, i, 1);
    }
    return group;
}

bool SmartSimWidget::ensureSimulator()
{
    if (!m_programLoaded || m_simBinary.isEmpty()) {
        appendLog("no program downloaded to SmartSim. Connect, then use Download first.");
        return false;
    }
    if (!QFileInfo::exists(m_simBinary)) {
        appendLog("downloaded SmartSim program is missing. Download again.");
        m_programLoaded = false;
        return false;
    }

    return startRuntimeProcess();
}

bool SmartSimWidget::startRuntimeProcess()
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
            emit simulationConnectionChanged(false);
        });
    }

    if (m_process->state() == QProcess::Running)
        return true;

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
    QFile pousCodeFile(outDir + "/POUS.c");
    QFile resourceFile(outDir + "/resource1.c");
    if (!pousFile.open(QFile::ReadOnly | QFile::Text)
        || !pousCodeFile.open(QFile::ReadOnly | QFile::Text)
        || !resourceFile.open(QFile::ReadOnly | QFile::Text)) {
        if (error) *error = "cannot read POUS.h, POUS.c or resource1.c";
        return false;
    }

    const QString pous = QString::fromUtf8(pousFile.readAll());
    const QString pousCode = QString::fromUtf8(pousCodeFile.readAll());
    const QString resource = QString::fromUtf8(resourceFile.readAll());

    QRegularExpression instanceRe("\\b([A-Z][A-Z0-9_]*)\\s+(RESOURCE1__[A-Z0-9_]+);");
    QRegularExpressionMatch instanceMatch = instanceRe.match(resource);
    if (!instanceMatch.hasMatch()) {
        if (error) *error = "cannot find RESOURCE1 program instance";
        return false;
    }

    const QString programType = instanceMatch.captured(1);
    const QString instanceName = instanceMatch.captured(2);
    QMap<QString, QString> structBodies;
    QRegularExpression structRe("typedef\\s+struct\\s*\\{([\\s\\S]*?)\\}\\s*([A-Z][A-Z0-9_]*)\\s*;");
    QRegularExpressionMatchIterator structIt = structRe.globalMatch(pous);
    while (structIt.hasNext()) {
        const QRegularExpressionMatch m = structIt.next();
        structBodies.insert(m.captured(2), m.captured(1));
    }
    const QString programBody = structBodies.value(programType);
    if (programBody.isEmpty()) {
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

    QMap<QString, QList<QPair<QString, int>>> sfcStepsByType;
    for (auto typeIt = structBodies.cbegin(); typeIt != structBodies.cend(); ++typeIt) {
        const QString type = typeIt.key();
        const QRegularExpression bodyStartRe(
            QString("\\bvoid\\s+%1_body__\\s*\\(").arg(QRegularExpression::escape(type)));
        const QRegularExpressionMatch bodyStart = bodyStartRe.match(pousCode);
        if (!bodyStart.hasMatch())
            continue;

        const QString initAndDefines = pousCode.left(bodyStart.capturedStart());
        const QRegularExpression initRe(
            QString("\\bvoid\\s+%1_init__\\s*\\(").arg(QRegularExpression::escape(type)));
        QRegularExpressionMatchIterator initIt = initRe.globalMatch(initAndDefines);
        int initStart = -1;
        while (initIt.hasNext())
            initStart = initIt.next().capturedStart();
        if (initStart < 0)
            continue;

        const QString segment = pousCode.mid(initStart, bodyStart.capturedStart() - initStart);
        QList<QPair<QString, int>> steps;
        QRegularExpression stepRe("#define\\s+([A-Z][A-Z0-9_]*)\\s+__step_list\\[(\\d+)\\]");
        QRegularExpressionMatchIterator stepIt = stepRe.globalMatch(segment);
        while (stepIt.hasNext()) {
            const QRegularExpressionMatch m = stepIt.next();
            steps.append(qMakePair(m.captured(1), m.captured(2).toInt()));
        }
        if (!steps.isEmpty())
            sfcStepsByType.insert(type, steps);
    }

    QStringList entries;
    QSet<QString> seenNames;
    auto addEntry = [&](const QString& displayName,
                        const QString& simType,
                        const QString& addressExpr) {
        if (seenNames.contains(displayName))
            return;
        seenNames.insert(displayName);
        entries << QString("    {\"%1\", %2, &%3, 0, 0.0},")
            .arg(displayName, simType, addressExpr);
    };

    std::function<void(const QString&, const QString&, const QString&, int)> addStructVars;
    addStructVars = [&](const QString& type,
                        const QString& displayPrefix,
                        const QString& addressPrefix,
                        int depth) {
        if (depth > 4)
            return;

        const QString body = structBodies.value(type);
        if (body.isEmpty())
            return;

        QRegularExpression varRe("__DECLARE_VAR\\(([^,]+),([^)]+)\\)");
        QRegularExpressionMatchIterator varIt = varRe.globalMatch(body);
        while (varIt.hasNext()) {
            const QRegularExpressionMatch m = varIt.next();
            const QString iecType = m.captured(1).trimmed();
            const QString name = m.captured(2).trimmed();
            if (name == "EN" || name == "ENO")
                continue;

            const QString simType = supported.value(iecType);
            if (!simType.isEmpty()) {
                addEntry(displayPrefix + "." + name,
                         simType,
                         addressPrefix + "." + name + ".value");
            }
        }

        const QList<QPair<QString, int>> steps = sfcStepsByType.value(type);
        for (const auto& step : steps) {
            addEntry(displayPrefix + "." + step.first + ".X",
                     "SIM_VAR_BOOL",
                     QString("%1.__step_list[%2].X.value").arg(addressPrefix).arg(step.second));
        }

        QRegularExpression childFbRe("^\\s*([A-Z][A-Z0-9_]*)\\s+([A-Z][A-Z0-9_]*)\\s*;",
                                     QRegularExpression::MultilineOption);
        QRegularExpressionMatchIterator childIt = childFbRe.globalMatch(body);
        while (childIt.hasNext()) {
            const QRegularExpressionMatch m = childIt.next();
            const QString childType = m.captured(1).trimmed();
            const QString childName = m.captured(2).trimmed();
            if (!structBodies.contains(childType))
                continue;
            addStructVars(childType,
                          displayPrefix + "." + childName,
                          addressPrefix + "." + childName,
                          depth + 1);
        }
    };

    addStructVars(programType, "main", instanceName, 0);

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
    m_debugSession->requestStatusAndValues();
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

    m_debugSession->handleRuntimeReply(doc.object());
}

void SmartSimWidget::handleRuntimeVariables(const QVector<SimDebugValue>& vars)
{
    QMap<QString, SimDebugValue> values;
    for (const SimDebugValue& var : vars) {
        values.insert(var.name, var);
    }
    m_lastValues = values;

    for (QLabel* led : m_diLeds)
        led->setStyleSheet(ledStyle(false, "#22c55e"));
    for (QLabel* led : m_doLeds)
        led->setStyleSheet(ledStyle(false, "#38bdf8"));
    for (QProgressBar* bar : m_aiBars)
        bar->setValue(0);
    for (QProgressBar* bar : m_aoBars)
        bar->setValue(0);

    auto setDigital = [&values](const QStringList& mappedVars,
                                QList<QLabel*>& leds,
                                const QString& color) {
        const int count = qMin(mappedVars.size(), leds.size());
        for (int i = 0; i < count; ++i) {
            if (mappedVars.at(i).isEmpty())
                continue;
            const SimDebugValue var = values.value(mappedVars.at(i));
            leds[i]->setStyleSheet(SmartSimWidget::ledStyle(var.value.toBool(), color));
        }
    };

    auto setAnalog = [&values](const QStringList& mappedVars,
                               QList<QProgressBar*>& bars) {
        const int count = qMin(mappedVars.size(), bars.size());
        for (int i = 0; i < count; ++i) {
            if (mappedVars.at(i).isEmpty())
                continue;
            const SimDebugValue var = values.value(mappedVars.at(i));
            double raw = var.value.toDouble();
            int scaled = 0;
            if (raw >= 0.0 && raw <= 10.0)
                scaled = static_cast<int>(raw * 1000.0);
            else
                scaled = static_cast<int>(raw);
            bars[i]->setValue(qBound(0, scaled, 10000));
        }
    };

    setDigital(m_diVars, m_diLeds, "#22c55e");
    setDigital(m_doVars, m_doLeds, "#38bdf8");
    setAnalog(m_aiVars, m_aiBars);
    setAnalog(m_aoVars, m_aoBars);

    emit debugValuesChanged(vars);
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
    emit simulationRunStateChanged(stateText(m_state));
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
    emit simulationEvent(line);
}

bool SmartSimWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        const QVariant indexValue = watched->property("simDiIndex");
        if (indexValue.isValid()) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                toggleDigitalInput(indexValue.toInt());
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void SmartSimWidget::toggleDigitalInput(int index)
{
    if (index < 0 || index >= m_diVars.size())
        return;

    const QString name = m_diVars.at(index);
    if (name.isEmpty()) {
        appendLog(QString("DI%1 is not mapped to a variable.").arg(index));
        return;
    }
    if (!m_debugSession) {
        appendLog("SmartSim debug session is not ready.");
        return;
    }
    if (!m_process || m_process->state() != QProcess::Running) {
        appendLog("SmartSim program is not loaded. Download first.");
        return;
    }

    const SimDebugValue var = m_lastValues.value(name);
    const bool nextValue = !var.value.toBool();
    m_debugSession->forceVariable(name, nextValue);
    appendLog(QString("DI%1 forced %2 = %3.")
                  .arg(index)
                  .arg(name)
                  .arg(nextValue ? "TRUE" : "FALSE"));
    requestVariables();
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
