#include "StGenerator.h"
#include <QDomDocument>
#include <QFile>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════════════
// 模块内部实现（匿名命名空间）
// ═══════════════════════════════════════════════════════════════════════════
namespace {

static QString g_lastError;

// ───────────────────────────────────────────────────────────────────────────
// DOM 辅助
// ───────────────────────────────────────────────────────────────────────────

// 按本地名查找第一个子元素（忽略命名空间前缀）
static QString elemName(const QDomElement& e)
{
    const QString local = e.localName();
    return local.isEmpty() ? e.tagName() : local;
}

static QDomElement fc(const QDomElement& p, const QString& localName)
{
    for (QDomElement c = p.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        if (elemName(c) == localName) return c;
    return {};
}

// 按本地名收集所有直接子元素
static QList<QDomElement> ch(const QDomElement& p, const QString& localName)
{
    QList<QDomElement> r;
    for (QDomElement c = p.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        if (elemName(c) == localName) r << c;
    return r;
}

// 从 <ST>/<IL> 等语言元素内的 <xhtml:p> 提取 CDATA 文本
static QString cdata(const QDomElement& langEl)
{
    for (QDomElement c = langEl.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        if (elemName(c) == "p") return c.text();
    return {};
}

// 从 <type> 元素提取 IEC 类型字符串
static QString itype(const QDomElement& typeEl)
{
    if (typeEl.isNull()) return "ANY";
    QDomElement child = typeEl.firstChildElement();
    if (child.isNull()) return "ANY";
    if (elemName(child) == "derived") {
        const QString name = child.attribute("name");
        if (name == "HMI_BOOL") return "BOOL";
        return name;
    }
    if (elemName(child) == "array") {
        QString base = itype(fc(child, "baseType"));
        return QString("ARRAY OF %1").arg(base);
    }
    return elemName(child); // BOOL INT REAL DINT WORD TIME …
}

// ───────────────────────────────────────────────────────────────────────────
// 生成变量声明块
// ───────────────────────────────────────────────────────────────────────────
static void emitVarBlock(const QDomElement& varsEl,
                          const QString& keyword,
                          bool isConst,
                          QStringList& out,
                          const QString& indent = "")
{
    if (varsEl.isNull()) return;
    auto vars = ch(varsEl, "variable");
    if (vars.isEmpty()) return;

    out << indent + (isConst ? keyword + " CONSTANT" : keyword);
    for (const QDomElement& v : vars) {
        QString name = v.attribute("name");
        QString type = itype(fc(v, "type"));
        // 初始值
        QString init;
        QDomElement iv = fc(v, "initialValue");
        if (!iv.isNull()) {
            QDomElement sv = fc(iv, "simpleValue");
            if (!sv.isNull()) init = " := " + sv.attribute("value");
        }
        out << indent + QString("  %1 : %2%3;").arg(name, type, init);
    }
    out << indent + "END_VAR";
}

static void emitBoolTemps(const QStringList& names, QStringList& out)
{
    if (names.isEmpty()) return;
    out << "VAR";
    for (const QString& name : names)
        out << QString("  %1 : BOOL;").arg(name);
    out << "END_VAR";
}

// ───────────────────────────────────────────────────────────────────────────
// FBD/LD 图元连接结构
// ───────────────────────────────────────────────────────────────────────────
struct Conn {
    int     refId  = -1;    // 来源图元 localId（-1 = 未连接）
    QString refPort;         // 来源图元的输出端口名（空 = 首端口）
    QString param;           // 本输入的形式参数名
};

struct Elem {
    enum Kind { InVar, OutVar, InOutVar,
                Block, Contact, Coil, PowerRail, Skip };
    Kind    kind      = Skip;
    int     localId   = 0;
    int     execOrder = 0;

    QString typeName;       // Block: 类型名
    QString instanceName;   // Block: 实例名（空 = 函数调用）
    QString expression;     // InVar/OutVar/InOutVar/Contact/Coil
    bool    negated = false;
    bool    negatedIn = false;
    bool    negatedOut = false;
    QString edge;           // Contact: none/rising/falling
    QString storage;        // Coil: none/set/reset

    QList<Conn>    inputs;
    QList<QString> outputPorts;

    // 代码生成期间填充：输出端口名 → 已解析的 ST 信号表达式
    QMap<QString, QString> outSig;
};

static QList<Conn> inputConnections(const QDomElement& owner)
{
    QList<Conn> inputs;
    const QDomElement cpi = fc(owner, "connectionPointIn");
    for (const QDomElement& con : ch(cpi, "connection")) {
        bool ok = false;
        const int refId = con.attribute("refLocalId").toInt(&ok);
        if (!ok) continue;
        inputs << Conn{refId, con.attribute("formalParameter"), {}};
    }
    return inputs;
}

static QList<Conn> blockInputConnections(const QDomElement& inputVar)
{
    QList<Conn> inputs;
    const QString param = inputVar.attribute("formalParameter");
    const QDomElement cpi = fc(inputVar, "connectionPointIn");
    for (const QDomElement& con : ch(cpi, "connection")) {
        bool ok = false;
        const int refId = con.attribute("refLocalId").toInt(&ok);
        if (!ok) continue;
        inputs << Conn{refId, con.attribute("formalParameter"), param};
    }
    return inputs;
}

// ───────────────────────────────────────────────────────────────────────────
// 解析 FBD/LD 体内的所有图元
// ───────────────────────────────────────────────────────────────────────────
static QMap<int, Elem> parseFbd(const QDomElement& bodyEl)
{
    QMap<int, Elem> map;
    for (QDomElement e = bodyEl.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement())
    {
        const QString tag = elemName(e);
        Elem el;
        el.localId   = e.attribute("localId").toInt();
        el.execOrder = e.attribute("executionOrderId", "0").toInt();

        if (tag == "inVariable") {
            el.kind       = Elem::InVar;
            el.expression = fc(e, "expression").text().trimmed();
            el.negated    = (e.attribute("negated") == "true");
        }
        else if (tag == "outVariable") {
            el.kind       = Elem::OutVar;
            el.expression = fc(e, "expression").text().trimmed();
            el.negated    = (e.attribute("negated") == "true");
            el.inputs     = inputConnections(e);
        }
        else if (tag == "inOutVariable") {
            el.kind       = Elem::InOutVar;
            el.expression = fc(e, "expression").text().trimmed();
            el.negatedIn  = (e.attribute("negatedIn") == "true");
            el.negatedOut = (e.attribute("negatedOut") == "true");
            el.inputs     = inputConnections(e);
        }
        else if (tag == "block") {
            el.kind         = Elem::Block;
            el.typeName     = e.attribute("typeName");
            el.instanceName = e.attribute("instanceName");

            QDomElement inVars = fc(e, "inputVariables");
            for (const QDomElement& v : ch(inVars, "variable")) {
                el.inputs << blockInputConnections(v);
            }
            QDomElement outVars = fc(e, "outputVariables");
            for (const QDomElement& v : ch(outVars, "variable"))
                el.outputPorts << v.attribute("formalParameter");
        }
        else if (tag == "contact") {
            el.kind       = Elem::Contact;
            el.expression = fc(e, "variable").text().trimmed();
            el.negated    = (e.attribute("negated") == "true");
            el.edge       = e.attribute("edge", "none");
            el.inputs     = inputConnections(e);
        }
        else if (tag == "coil") {
            el.kind       = Elem::Coil;
            el.expression = fc(e, "variable").text().trimmed();
            el.negated    = (e.attribute("negated") == "true");
            el.storage    = e.attribute("storage", "none");
            el.inputs     = inputConnections(e);
        }
        else if (tag == "leftPowerRail") {
            el.kind = Elem::PowerRail;
        }
        else {
            el.kind = Elem::Skip;
        }

        if (el.kind != Elem::Skip)
            map[el.localId] = el;
    }
    return map;
}

// ───────────────────────────────────────────────────────────────────────────
// 拓扑排序（两阶段 Kahn 算法）
//
// 阶段1：先做完整依赖图的 Kahn 排序，找出哪些节点在环路中
// 阶段2：对环路中的 InOutVar→Block 连接打断（视为旧值反馈）
//         对非环路的 InOutVar 连接保留依赖（使用赋值后的新值）
// ───────────────────────────────────────────────────────────────────────────
static QList<int> topoSort(const QMap<int, Elem>& elems)
{
    // ── 阶段1：完整图，找环路节点 ────────────────────────────────
    QMap<int, QSet<int>> fSuccs;
    QMap<int, int>       fIndeg;
    for (int id : elems.keys()) fIndeg[id] = 0;

    for (const auto& [id, el] : elems.asKeyValueRange()) {
        if (el.kind == Elem::InVar || el.kind == Elem::PowerRail) continue;
        for (const Conn& c : el.inputs) {
            if (c.refId < 0 || !elems.contains(c.refId)) continue;
            Elem::Kind sk = elems[c.refId].kind;
            if (sk == Elem::InVar || sk == Elem::PowerRail) continue;
            fSuccs[c.refId].insert(id);
            fIndeg[id]++;
        }
    }
    {
        QList<int> q;
        for (auto it = fIndeg.cbegin(); it != fIndeg.cend(); ++it)
            if (it.value() == 0) q << it.key();
        while (!q.isEmpty()) {
            int cur = q.takeFirst();
            for (int s : fSuccs[cur])
                if (--fIndeg[s] == 0) q << s;
        }
    }
    // fIndeg[id] > 0 的节点处于环路中
    QSet<int> inCycle;
    for (auto it = fIndeg.cbegin(); it != fIndeg.cend(); ++it)
        if (it.value() > 0) inCycle.insert(it.key());

    // ── 阶段2：约简图（打断反馈边）+ Kahn 排序 ────────────────────
    QMap<int, QSet<int>> succs;
    QMap<int, int>       indeg;
    for (int id : elems.keys()) indeg[id] = 0;

    for (const auto& [id, el] : elems.asKeyValueRange()) {
        if (el.kind == Elem::InVar || el.kind == Elem::PowerRail) continue;
        for (const Conn& c : el.inputs) {
            if (c.refId < 0 || !elems.contains(c.refId)) continue;
            Elem::Kind sk = elems[c.refId].kind;
            if (sk == Elem::InVar || sk == Elem::PowerRail) continue;
            // 打断反馈边：来源是环路中的 InOutVar，且目标不是 OutVar
            if (sk == Elem::InOutVar && inCycle.contains(c.refId)
                && el.kind != Elem::OutVar)
                continue;
            succs[c.refId].insert(id);
            indeg[id]++;
        }
    }

    QList<int> queue, result;
    for (auto it = indeg.cbegin(); it != indeg.cend(); ++it)
        if (it.value() == 0) queue << it.key();
    std::sort(queue.begin(), queue.end());

    while (!queue.isEmpty()) {
        int cur = queue.takeFirst();
        result << cur;
        QList<int> ss(succs[cur].begin(), succs[cur].end());
        std::sort(ss.begin(), ss.end());
        for (int s : ss) {
            if (--indeg[s] == 0) {
                queue << s;
                std::sort(queue.begin(), queue.end());
            }
        }
    }
    for (int id : elems.keys())
        if (!result.contains(id)) result << id;
    return result;
}

// ───────────────────────────────────────────────────────────────────────────
// FBD/LD → ST 代码生成
// ───────────────────────────────────────────────────────────────────────────
struct FbdStResult {
    QStringList lines;
    QStringList boolTemps;
    QStringList edgeTemps;
};

static FbdStResult fbdToSt(QMap<int, Elem>& elems)
{
    FbdStResult result;
    int tmpN = 0;

    auto newBoolTemp = [&]() -> QString {
        QString name = QString("TIZI_TMP%1").arg(++tmpN);
        result.boolTemps << name;
        return name;
    };

    auto edgeTemp = [&](int localId) -> QString {
        QString name = QString("TIZI_EDGE%1").arg(localId);
        if (!result.edgeTemps.contains(name))
            result.edgeTemps << name;
        return name;
    };

    // 预处理：统计每个图元输出端口被引用的次数
    // refId→port 的引用数；函数调用被多次引用时需要临时变量
    QMap<QPair<int,QString>, int> useCount;
    for (const auto& [id, el] : elems.asKeyValueRange()) {
        for (const Conn& c : el.inputs) {
            if (c.refId < 0) continue;
            useCount[{c.refId, c.refPort}]++;
        }
    }

    // 查询信号表达式
    auto sig = [&](int refId, const QString& refPort) -> QString {
        if (refId < 0 || !elems.contains(refId)) return QString();
        Elem& src = elems[refId];
        switch (src.kind) {
        case Elem::InVar:
            return src.negated
                ? QString("NOT %1").arg(src.expression)
                : src.expression;
        case Elem::InOutVar:
            return src.negatedOut
                ? QString("NOT %1").arg(src.expression)
                : src.expression;
        case Elem::PowerRail:
            return "TRUE";
        case Elem::Block: {
            QString p = refPort.isEmpty()
                ? (src.outputPorts.isEmpty() ? "OUT" : src.outputPorts.first())
                : refPort;
            return src.outSig.value(p);
        }
        case Elem::Contact:
            return src.outSig.value({});
        default:
            return QString();
        }
    };

    auto inputSignal = [&](const Elem& el, const QString& defaultValue) -> QString {
        QStringList parts;
        for (const Conn& c : el.inputs) {
            QString s = sig(c.refId, c.refPort);
            if (!s.isEmpty()) parts << s;
        }
        if (parts.isEmpty()) return defaultValue;
        if (parts.size() == 1) return parts.first();

        for (QString& part : parts)
            part = QString("(%1)").arg(part);
        return parts.join(" OR ");
    };

    auto connSignal = [&](const Conn& c) -> QString {
        return sig(c.refId, c.refPort);
    };

    auto mergeSignals = [&](const QStringList& values,
                            const QString& defaultValue) -> QString {
        QStringList parts;
        for (const QString& s : values) {
            if (!s.isEmpty()) parts << s;
        }
        if (parts.isEmpty()) return defaultValue;
        if (parts.size() == 1) return parts.first();

        for (QString& part : parts)
            part = QString("(%1)").arg(part);
        return parts.join(" OR ");
    };

    QList<int> order = topoSort(elems);

    for (int id : order) {
        Elem& el = elems[id];
        switch (el.kind) {
        case Elem::InVar:
        case Elem::PowerRail:
            break;

        case Elem::InOutVar: {
            QString s = inputSignal(el, {});
            if (!s.isEmpty() && el.negatedIn)
                s = QString("NOT (%1)").arg(s);
            if (!s.isEmpty() && s != el.expression)
                result.lines << QString("  %1 := %2;").arg(el.expression, s);
            break;
        }

        case Elem::Contact: {
            QString in = inputSignal(el, "TRUE");
            QString varExpr;
            if (el.edge == "rising" || el.edge == "falling") {
                const QString prev = edgeTemp(el.localId);
                const QString rawPulse = (el.edge == "rising")
                    ? QString("(%1 AND NOT %2)").arg(el.expression, prev)
                    : QString("((NOT %1) AND %2)").arg(el.expression, prev);
                QString pulse = rawPulse;
                if (el.negated)
                    pulse = QString("NOT %1").arg(rawPulse);
                QString tmp = newBoolTemp();
                result.lines << QString("  %1 := %2;").arg(tmp, pulse);
                result.lines << QString("  %1 := %2;").arg(prev, el.expression);
                varExpr = tmp;
            } else {
                varExpr = el.negated
                            ? QString("NOT %1").arg(el.expression)
                            : el.expression;
            }
            if (in == "TRUE") {
                // 直接使用变量表达式，无需临时变量
                el.outSig[{}] = varExpr;
            } else {
                // 串联逻辑：需要 AND 表达式
                QString tmp = newBoolTemp();
                el.outSig[{}] = tmp;
                result.lines << QString("  %1 := (%2) AND %3;").arg(tmp, in, varExpr);
            }
            break;
        }

        case Elem::Coil: {
            QString in = inputSignal(el, "FALSE");
            QString val = el.negated ? QString("NOT (%1)").arg(in) : in;
            if (el.storage == "set") {
                result.lines << QString("  IF %1 THEN").arg(val);
                result.lines << QString("    %1 := TRUE;").arg(el.expression);
                result.lines << "  END_IF;";
            } else if (el.storage == "reset") {
                result.lines << QString("  IF %1 THEN").arg(val);
                result.lines << QString("    %1 := FALSE;").arg(el.expression);
                result.lines << "  END_IF;";
            } else {
                result.lines << QString("  %1 := %2;").arg(el.expression, val);
            }
            break;
        }

        case Elem::Block: {
            QMap<QString, QStringList> namedInputs;
            QStringList positionalArgs;
            for (const Conn& c : el.inputs) {
                if (c.refId < 0) continue;
                QString s = connSignal(c);
                if (s.isEmpty()) s = "FALSE";
                if (!c.param.isEmpty())
                    namedInputs[c.param] << s;
                else
                    positionalArgs << s;
            }

            QStringList args;
            for (const auto& [param, values] : namedInputs.asKeyValueRange()) {
                QString s = mergeSignals(values, "FALSE");
                args << QString("%1 := %2").arg(param, s);
            }
            args << positionalArgs;

            if (el.instanceName.isEmpty()) {
                // 函数调用：检查输出是否被多次引用
                QString port = el.outputPorts.isEmpty() ? "OUT" : el.outputPorts.first();
                int uses = useCount.value({el.localId, {}}, 0)
                         + useCount.value({el.localId, port}, 0);
                QString callExpr;
                if (el.typeName == "NOT"
                    && namedInputs.contains("IN")
                    && namedInputs.size() == 1
                    && positionalArgs.isEmpty()) {
                    callExpr = QString("NOT (%1)").arg(mergeSignals(namedInputs.value("IN"), "FALSE"));
                } else {
                    callExpr = QString("%1(%2)").arg(el.typeName, args.join(", "));
                }
                if (uses > 1) {
                    // 多次引用：需要临时变量
                    // Without type information for arbitrary function outputs,
                    // repeated function references are inlined instead of using
                    // an undeclared or incorrectly typed temporary.
                    el.outSig[port] = callExpr;
                } else {
                    // 单次引用：内联表达式（不生成赋值语句）
                    el.outSig[port] = callExpr;
                }
            } else {
                // 功能块调用（有状态，必须显式调用）
                result.lines << QString("  %1(%2);")
                         .arg(el.instanceName, args.join(", "));
                for (const QString& p : el.outputPorts)
                    el.outSig[p] = el.instanceName + "." + p;
            }
            break;
        }

        case Elem::OutVar: {
            QString s = inputSignal(el, "FALSE");
            if (!s.isEmpty() && el.negated)
                s = QString("NOT (%1)").arg(s);
            if (!s.isEmpty())
                result.lines << QString("  %1 := %2;").arg(el.expression, s);
            break;
        }

        default: break;
        }
    }

    return result;
}
// ───────────────────────────────────────────────────────────────────────────
// SFC → matiec 原生 SFC 文本
// ───────────────────────────────────────────────────────────────────────────
static QStringList sfcToText(const QDomElement& sfcEl,
                             const QMap<QString, QString>& namedTransitions = QMap<QString, QString>())
{
    QStringList out;

    struct StepInfo {
        QString name;
        bool    initial = false;
    };
    QMap<int, StepInfo>      steps;
    QMap<int, QString>       transCond;  // localId → ST 条件
    QMap<int, QList<QString>> stepActCalls; // stepLocalId → action calls
    QList<QPair<QString, QStringList>> inlineActionDefs;

    auto actionCall = [](const QString& name,
                         const QString& qualifier,
                         const QString& duration) -> QString {
        const QString q = qualifier.trimmed().isEmpty() ? "N" : qualifier.trimmed();
        if (!duration.trimmed().isEmpty())
            return QString("%1(%2, %3);").arg(name, q, duration.trimmed());
        return QString("%1(%2);").arg(name, q);
    };

    // 解析所有节点
    for (QDomElement e = sfcEl.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement())
    {
        QString tag = elemName(e);
        int id = e.attribute("localId").toInt();

        if (tag == "step") {
            steps[id] = { e.attribute("name"),
                          e.attribute("initialStep") == "true" };
        }
        else if (tag == "transition") {
            QDomElement cond = fc(e, "condition");
            QDomElement inl  = fc(cond, "inline");
            QDomElement stEl = fc(inl, "ST");
            QString conditionText = cdata(stEl).trimmed();
            if (conditionText.isEmpty()) {
                QDomElement ref = fc(cond, "reference");
                const QString refName = ref.attribute("name").trimmed();
                if (!refName.isEmpty())
                    conditionText = namedTransitions.value(refName, refName);
            }
            transCond[id] = conditionText;
        }
        else if (tag == "actionBlock") {
            QDomElement cpi = fc(e, "connectionPointIn");
            QDomElement con = fc(cpi, "connection");
            int stepId = con.isNull() ? -1 : con.attribute("refLocalId").toInt();
            QList<QString> acts;
            for (const QDomElement& act : ch(e, "action")) {
                const QString qualifier = act.attribute("qualifier", "N");
                const QString duration = act.attribute("duration");
                QDomElement inl  = fc(act, "inline");
                QDomElement stEl = fc(inl, "ST");
                QString code = cdata(stEl).trimmed();
                if (!code.isEmpty()) {
                    const QString stepName = steps.value(stepId).name;
                    const QString actionName = QString("%1_act%2").arg(stepName).arg(acts.size());
                    acts << actionCall(actionName, qualifier, duration);
                    inlineActionDefs << qMakePair(actionName, code.split('\n'));
                    continue;
                }

                QDomElement ref = fc(act, "reference");
                const QString refName = ref.attribute("name").trimmed();
                if (!refName.isEmpty())
                    acts << actionCall(refName, qualifier, duration);
            }
            if (stepId >= 0) stepActCalls[stepId] = acts;
        }
    }

    // 建立连接图：nodeId → 出口节点列表
    QMap<int, QList<int>> nodeOut;
    for (QDomElement e = sfcEl.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement())
    {
        int id = e.attribute("localId").toInt();
        // connectionPointIn 指向该节点的上游
        for (const QDomElement& cpi : ch(e, "connectionPointIn")) {
            QDomElement con = fc(cpi, "connection");
            if (!con.isNull())
                nodeOut[con.attribute("refLocalId").toInt()] << id;
        }
    }

    // jumpStep 目标名
    QMap<int, QString> jumpTarget;
    for (const QDomElement& e : ch(sfcEl, "jumpStep"))
        jumpTarget[e.attribute("localId").toInt()] = e.attribute("targetName");

    // ── 生成步骤定义 ──────────────────────────────────────────
    for (const auto& [id, s] : steps.asKeyValueRange()) {
        if (s.initial)
            out << QString("INITIAL_STEP %1:").arg(s.name);
        else
            out << QString("STEP %1:").arg(s.name);

        // 内联动作引用（生成唯一名称）
        auto acts = stepActCalls.value(id);
        for (const QString& act : acts)
            out << "  " + act;

        out << "END_STEP";
        out << "";
    }

    // ── 生成转换定义 ──────────────────────────────────────────
    for (const auto& [tid, cond] : transCond.asKeyValueRange()) {
        // 查找从哪些步骤到哪些步骤
        QStringList fromNames, toNames;

        // from: 连接到本转换的上游节点（步骤或分支）
        for (QDomElement e = sfcEl.firstChildElement();
             !e.isNull(); e = e.nextSiblingElement())
        {
            if (e.attribute("localId").toInt() != tid) continue;
            for (const QDomElement& cpi : ch(e, "connectionPointIn")) {
                QDomElement con = fc(cpi, "connection");
                if (con.isNull()) continue;
                int srcId = con.attribute("refLocalId").toInt();
                if (steps.contains(srcId))
                    fromNames << steps[srcId].name;
                // selectionDivergence: 查找其上游步骤
                else {
                    for (QDomElement e2 = sfcEl.firstChildElement();
                         !e2.isNull(); e2 = e2.nextSiblingElement())
                    {
                        if (e2.attribute("localId").toInt() != srcId) continue;
                        for (const QDomElement& cpi2 : ch(e2, "connectionPointIn")) {
                            QDomElement con2 = fc(cpi2, "connection");
                            if (!con2.isNull()) {
                                int s2 = con2.attribute("refLocalId").toInt();
                                if (steps.contains(s2))
                                    fromNames << steps[s2].name;
                            }
                        }
                    }
                }
            }
            break;
        }

        // to: 本转换的出口节点（步骤、jumpStep 或 convergence）
        for (int dst : nodeOut.value(tid)) {
            if (steps.contains(dst))
                toNames << steps[dst].name;
            else if (jumpTarget.contains(dst))
                toNames << jumpTarget[dst];
            else {
                // selectionConvergence：查找其出口的 jumpStep 或步骤
                for (int dst2 : nodeOut.value(dst)) {
                    if (steps.contains(dst2))
                        toNames << steps[dst2].name;
                    else if (jumpTarget.contains(dst2))
                        toNames << jumpTarget[dst2];
                }
            }
        }

        if (fromNames.isEmpty() || toNames.isEmpty()) continue;

        QString from = fromNames.size() == 1 ? fromNames.first()
                     : "(" + fromNames.join(", ") + ")";
        QString to   = toNames.size()   == 1 ? toNames.first()
                     : "(" + toNames.join(", ") + ")";

        out << QString("TRANSITION FROM %1 TO %2").arg(from, to);
        out << QString("  := %1;").arg(cond.isEmpty() ? "TRUE" : cond);
        out << "END_TRANSITION";
        out << "";
    }

    // ── 生成内联动作定义 ──────────────────────────────────────
    for (const auto& actionDef : inlineActionDefs) {
        out << QString("ACTION %1:").arg(actionDef.first);
        for (const QString& line : actionDef.second)
            out << "  " + line;
        out << "END_ACTION";
        out << "";
    }

    return out;
}

struct ActionStResult {
    QStringList lines;
    QStringList boolTemps;
    QStringList edgeTemps;
};

static ActionStResult convertPouActions(const QDomElement& pouEl)
{
    ActionStResult result;
    QDomElement actionsEl = fc(pouEl, "actions");
    if (actionsEl.isNull()) return result;

    for (const QDomElement& actionEl : ch(actionsEl, "action")) {
        const QString actionName = actionEl.attribute("name").trimmed();
        if (actionName.isEmpty()) continue;

        QDomElement body = fc(actionEl, "body");
        if (body.isNull()) continue;

        result.lines << QString("  ACTION %1:").arg(actionName);

        QDomElement fbdEl = fc(body, "FBD");
        if (fbdEl.isNull()) fbdEl = fc(body, "LD");
        if (!fbdEl.isNull()) {
            auto elems = parseFbd(fbdEl);
            FbdStResult fbdResult = fbdToSt(elems);
            result.boolTemps << fbdResult.boolTemps;
            result.edgeTemps << fbdResult.edgeTemps;
            for (const QString& ln : fbdResult.lines)
                result.lines << "  " + ln;
        } else {
            QDomElement stEl = fc(body, "ST");
            QDomElement ilEl = fc(body, "IL");
            QDomElement sfcEl = fc(body, "SFC");
            if (!stEl.isNull()) {
                for (const QString& ln : cdata(stEl).split('\n'))
                    result.lines << "    " + ln;
            } else if (!ilEl.isNull()) {
                for (const QString& ln : cdata(ilEl).split('\n'))
                    result.lines << "    " + ln;
            } else if (!sfcEl.isNull()) {
                for (const QString& ln : sfcToText(sfcEl))
                    result.lines << "  " + ln;
            }
        }

        result.lines << "  END_ACTION";
        result.lines << "";
    }

    return result;
}

static QString firstAssignmentExpression(const QStringList& lines, const QString& preferredTarget)
{
    QString fallback;
    for (QString line : lines) {
        line = line.trimmed();
        if (!line.endsWith(';')) continue;
        line.chop(1);
        const int assignPos = line.indexOf(":=");
        if (assignPos < 0) continue;

        const QString target = line.left(assignPos).trimmed();
        const QString expr = line.mid(assignPos + 2).trimmed();
        if (expr.isEmpty()) continue;
        if (!preferredTarget.isEmpty() && target == preferredTarget)
            return expr;
        if (fallback.isEmpty())
            fallback = expr;
    }
    return fallback;
}

static QMap<QString, QString> namedTransitionExpressions(const QDomElement& pouEl)
{
    QMap<QString, QString> result;
    QDomElement transitionsEl = fc(pouEl, "transitions");
    if (transitionsEl.isNull()) return result;

    for (const QDomElement& transitionEl : ch(transitionsEl, "transition")) {
        const QString name = transitionEl.attribute("name").trimmed();
        if (name.isEmpty()) continue;

        QDomElement body = fc(transitionEl, "body");
        if (body.isNull()) continue;

        QDomElement stEl = fc(body, "ST");
        if (!stEl.isNull()) {
            const QString expr = cdata(stEl).trimmed();
            if (!expr.isEmpty())
                result[name] = expr;
            continue;
        }

        QDomElement fbdEl = fc(body, "FBD");
        if (fbdEl.isNull()) fbdEl = fc(body, "LD");
        if (!fbdEl.isNull()) {
            auto elems = parseFbd(fbdEl);
            FbdStResult fbdResult = fbdToSt(elems);
            const QString expr = firstAssignmentExpression(fbdResult.lines, name);
            if (!expr.isEmpty())
                result[name] = expr;
        }
    }

    return result;
}

// ───────────────────────────────────────────────────────────────────────────
// 生成单个 POU 的 ST 文本
// ───────────────────────────────────────────────────────────────────────────
static QStringList convertPou(const QDomElement& pouEl)
{
    QStringList out;
    const QString name    = pouEl.attribute("name");
    const QString pouType = pouEl.attribute("pouType");

    QDomElement iface = fc(pouEl, "interface");
    QDomElement body = fc(pouEl, "body");

    QDomElement fbdEl = fc(body, "FBD");
    if (fbdEl.isNull()) fbdEl = fc(body, "LD");
    FbdStResult fbdResult;
    bool hasFbdBody = !fbdEl.isNull();
    if (hasFbdBody) {
        auto elems = parseFbd(fbdEl);
        fbdResult = fbdToSt(elems);
    }
    ActionStResult actionResult = convertPouActions(pouEl);
    QMap<QString, QString> transitionExprs = namedTransitionExpressions(pouEl);

    // ── 头部关键字 ────────────────────────────────────────
    QString keyword, endKeyword;
    if (pouType == "function") {
        QDomElement ret = fc(iface, "returnType");
        QString retType = ret.isNull() ? "VOID" : itype(ret);
        keyword    = QString("FUNCTION %1 : %2").arg(name, retType);
        endKeyword = "END_FUNCTION";
    } else if (pouType == "functionBlock") {
        keyword    = QString("FUNCTION_BLOCK %1").arg(name);
        endKeyword = "END_FUNCTION_BLOCK";
    } else {
        keyword    = QString("PROGRAM %1").arg(name);
        endKeyword = "END_PROGRAM";
    }

    out << keyword;

    // ── 变量声明 ──────────────────────────────────────────
    emitVarBlock(fc(iface, "inputVars"),
                 "VAR_INPUT", false, out);
    emitVarBlock(fc(iface, "outputVars"),
                 "VAR_OUTPUT", false, out);
    emitVarBlock(fc(iface, "inOutVars"),
                 "VAR_IN_OUT", false, out);
    emitVarBlock(fc(iface, "localVars"),
                 "VAR", false, out);
    QStringList tempNames = fbdResult.boolTemps;
    for (const QString& tempName : actionResult.boolTemps)
        if (!tempNames.contains(tempName))
            tempNames << tempName;
    for (const QString& edgeName : fbdResult.edgeTemps)
        if (!tempNames.contains(edgeName))
            tempNames << edgeName;
    for (const QString& edgeName : actionResult.edgeTemps)
        if (!tempNames.contains(edgeName))
            tempNames << edgeName;
    emitBoolTemps(tempNames, out);
    {
        QDomElement ev = fc(iface, "externalVars");
        bool isConst = ev.attribute("constant") == "true";
        emitVarBlock(ev, "VAR_EXTERNAL", isConst, out);
    }

    // ── 程序体 ────────────────────────────────────────────
    // ST
    QDomElement stEl = fc(body, "ST");
    if (!stEl.isNull()) {
        QString code = cdata(stEl);
        for (const QString& ln : code.split('\n'))
            out << "  " + ln;
        for (const QString& ln : actionResult.lines)
            out << ln;
        out << endKeyword;
        out << "";
        return out;
    }

    // IL
    QDomElement ilEl = fc(body, "IL");
    if (!ilEl.isNull()) {
        QString code = cdata(ilEl);
        for (const QString& ln : code.split('\n'))
            out << "  " + ln;
        for (const QString& ln : actionResult.lines)
            out << ln;
        out << endKeyword;
        out << "";
        return out;
    }

    // SFC
    QDomElement sfcEl = fc(body, "SFC");
    if (!sfcEl.isNull()) {
        for (const QString& ln : sfcToText(sfcEl, transitionExprs))
            out << "  " + ln;
        for (const QString& ln : actionResult.lines)
            out << ln;
        out << endKeyword;
        out << "";
        return out;
    }

    // FBD / LD（统一处理）
    if (hasFbdBody) {
        for (const QString& ln : fbdResult.lines)
            out << ln;
        for (const QString& ln : actionResult.lines)
            out << ln;
        out << endKeyword;
        out << "";
        return out;
    }

    out << "  (* Unsupported body language *)";
    for (const QString& ln : actionResult.lines)
        out << ln;
    out << endKeyword;
    out << "";
    return out;
}

static void emitNativeVarBlock(const QList<QDomElement>& vars,
                               const QString& keyword,
                               QStringList& out)
{
    if (vars.isEmpty()) return;

    out << keyword;
    for (const QDomElement& v : vars) {
        QString init;
        if (!v.attribute("init").trimmed().isEmpty())
            init = " := " + v.attribute("init").trimmed();
        out << QString("  %1 : %2%3;")
               .arg(v.attribute("name"),
                    v.attribute("type", "BOOL"),
                    init);
    }
    out << "END_VAR";
}

static QString nativePouType(const QString& value)
{
    const QString t = value.trimmed();
    if (t.compare("function", Qt::CaseInsensitive) == 0) return "function";
    if (t.compare("program", Qt::CaseInsensitive) == 0) return "program";
    return "functionBlock";
}

static QStringList convertNativePou(const QDomElement& pouEl)
{
    QStringList out;
    const QString name = pouEl.attribute("name");
    const QString pouType = nativePouType(pouEl.attribute("type"));

    QString keyword, endKeyword;
    if (pouType == "function") {
        keyword = QString("FUNCTION %1 : BOOL").arg(name);
        endKeyword = "END_FUNCTION";
    } else if (pouType == "program") {
        keyword = QString("PROGRAM %1").arg(name);
        endKeyword = "END_PROGRAM";
    } else {
        keyword = QString("FUNCTION_BLOCK %1").arg(name);
        endKeyword = "END_FUNCTION_BLOCK";
    }
    out << keyword;

    QList<QDomElement> inputVars, outputVars, inOutVars, localVars, externalVars;
    QDomElement varsEl = pouEl.firstChildElement("variables");
    for (QDomElement v = varsEl.firstChildElement("var");
         !v.isNull(); v = v.nextSiblingElement("var"))
    {
        const QString cls = v.attribute("class").trimmed();
        if (cls.compare("Input", Qt::CaseInsensitive) == 0) inputVars << v;
        else if (cls.compare("Output", Qt::CaseInsensitive) == 0) outputVars << v;
        else if (cls.compare("InOut", Qt::CaseInsensitive) == 0) inOutVars << v;
        else if (cls.compare("External", Qt::CaseInsensitive) == 0) externalVars << v;
        else localVars << v;
    }

    QDomElement graphEl = pouEl.firstChildElement("graphical");
    FbdStResult fbdResult;
    bool hasGraphBody = false;
    if (!graphEl.isNull()) {
        QString graph = graphEl.text();
        int nl = graph.indexOf('\n');
        QString bodyXml = (nl >= 0) ? graph.mid(nl + 1) : graph;
        QDomDocument bodyDoc;
        if (bodyDoc.setContent(bodyXml)) {
            QDomElement body = bodyDoc.documentElement();
            if (elemName(body) == "FBD" || elemName(body) == "LD") {
                auto elems = parseFbd(body);
                fbdResult = fbdToSt(elems);
                hasGraphBody = true;
            } else if (elemName(body) == "SFC") {
                hasGraphBody = true;
                fbdResult.lines = sfcToText(body);
            }
        }
    }

    emitNativeVarBlock(inputVars, "VAR_INPUT", out);
    emitNativeVarBlock(outputVars, "VAR_OUTPUT", out);
    emitNativeVarBlock(inOutVars, "VAR_IN_OUT", out);
    emitNativeVarBlock(localVars, "VAR", out);
    QStringList tempNames = fbdResult.boolTemps;
    for (const QString& edgeName : fbdResult.edgeTemps)
        if (!tempNames.contains(edgeName))
            tempNames << edgeName;
    emitBoolTemps(tempNames, out);
    emitNativeVarBlock(externalVars, "VAR_EXTERNAL", out);

    if (hasGraphBody) {
        for (const QString& ln : fbdResult.lines)
            out << ln;
    } else {
        const QString code = pouEl.firstChildElement("code").text();
        for (const QString& ln : code.split('\n'))
            out << "  " + ln;
    }

    out << endKeyword;
    out << "";
    return out;
}

static QString doConvertNative(const QDomElement& root)
{
    QStringList out;
    out << "(* Generated by TiZi StGenerator - IEC 61131-3 Structured Text *)";
    out << "";

    QDomNodeList pouNodes = root.elementsByTagName("pou");
    QDomElement firstProgram;
    for (int i = 0; i < pouNodes.count(); ++i) {
        QDomElement pou = pouNodes.at(i).toElement();
        if (pou.isNull()) continue;
        const QString pouType = nativePouType(pou.attribute("type"));
        if (firstProgram.isNull() && pouType == "program")
            firstProgram = pou;
        out << QString("(* %1 : %2 *)")
               .arg(pou.attribute("name"), pouType);
        for (const QString& ln : convertNativePou(pou))
            out << ln;
    }

    if (!firstProgram.isNull()) {
        const QString progName = firstProgram.attribute("name");
        out << "CONFIGURATION config";
        out << "  RESOURCE resource1 ON PLC";
        out << "    TASK main_task(INTERVAL := T#10ms, PRIORITY := 0);";
        out << QString("    PROGRAM main_instance WITH main_task : %1;").arg(progName);
        out << "  END_RESOURCE";
        out << "END_CONFIGURATION";
        out << "";
    }

    g_lastError.clear();
    return out.join('\n');
}

// ───────────────────────────────────────────────────────────────────────────
// 主转换函数
// ───────────────────────────────────────────────────────────────────────────
static QString doConvert(const QString& xmlContent)
{
    QDomDocument doc;
    QString errMsg; int errLine = 0, errCol = 0;
    if (!doc.setContent(xmlContent, true, &errMsg, &errLine, &errCol)) {
        g_lastError = QString("XML parse error at line %1, col %2: %3")
                      .arg(errLine).arg(errCol).arg(errMsg);
        return {};
    }

    QDomElement root = doc.documentElement();
    if (elemName(root) == "TiZiProject") {
        return doConvertNative(root);
    }
    if (elemName(root) != "project") {
        g_lastError = "Root element is not <project>";
        return {};
    }

    QStringList out;
    out << "(* Generated by TiZi StGenerator - IEC 61131-3 Structured Text *)";
    out << "";

    QDomElement insts   = fc(root, "instances");
    QDomElement configs = fc(insts, "configurations");

    // ── POU 定义（必须在 CONFIGURATION 块之前）────────────────────────────
    QDomElement types = fc(root, "types");
    QDomElement pous  = fc(types, "pous");
    for (const QDomElement& pou : ch(pous, "pou")) {
        out << QString("(* %1 : %2 *)")
               .arg(pou.attribute("name"), pou.attribute("pouType"));
        for (const QString& ln : convertPou(pou))
            out << ln;
    }

    // ── CONFIGURATION 块 ──────────────────────────────────────────────────
    // VAR_GLOBAL 必须在 CONFIGURATION 内，不能出现在顶层
    for (const QDomElement& cfg : ch(configs, "configuration")) {
        out << QString("CONFIGURATION %1").arg(cfg.attribute("name", "config"));

        // 配置层全局变量
        for (const QDomElement& gv : ch(cfg, "globalVars")) {
            bool isConst = gv.attribute("constant") == "true";
            emitVarBlock(gv, "VAR_GLOBAL", isConst, out, "  ");
        }

        // RESOURCE 块
        for (const QDomElement& res : ch(cfg, "resource")) {
            out << QString("  RESOURCE %1 ON PLC").arg(res.attribute("name", "resource1"));

            // resource 层全局变量
            for (const QDomElement& gv : ch(res, "globalVars")) {
                bool isConst = gv.attribute("constant") == "true";
                emitVarBlock(gv, "VAR_GLOBAL", isConst, out, "    ");
            }

            // TASK 声明 + 关联的 PROGRAM 实例
            for (const QDomElement& task : ch(res, "task")) {
                const QString taskName = task.attribute("name");
                const QString interval = task.attribute("interval", "T#10ms");
                const QString priority = task.attribute("priority", "0");
                out << QString("    TASK %1(INTERVAL := %2, PRIORITY := %3);")
                       .arg(taskName, interval, priority);
                for (const QDomElement& pi : ch(task, "pouInstance")) {
                    out << QString("    PROGRAM %1 WITH %2 : %3;")
                           .arg(pi.attribute("name"), taskName,
                                pi.attribute("typeName"));
                }
            }

            // 直接挂在 resource 下的 pouInstance（无 task）
            for (const QDomElement& pi : ch(res, "pouInstance")) {
                out << QString("    PROGRAM %1 : %2;")
                       .arg(pi.attribute("name"), pi.attribute("typeName"));
            }

            out << "  END_RESOURCE";
        }

        out << "END_CONFIGURATION";
        out << "";
    }

    // ── 没有 CONFIGURATION 但有 PROGRAM POU：生成最小默认配置 ────────────
    if (ch(configs, "configuration").isEmpty()) {
        QDomElement firstProg;
        for (const QDomElement& pou : ch(pous, "pou")) {
            if (pou.attribute("pouType") == "program") {
                firstProg = pou;
                break;
            }
        }
        if (!firstProg.isNull()) {
            const QString progName = firstProg.attribute("name");
            out << "CONFIGURATION config";
            out << "  RESOURCE resource1 ON PLC";
            out << "    TASK main_task(INTERVAL := T#10ms, PRIORITY := 0);";
            out << QString("    PROGRAM main_instance WITH main_task : %1;").arg(progName);
            out << "  END_RESOURCE";
            out << "END_CONFIGURATION";
            out << "";
        }
    }

    g_lastError.clear();
    return out.join('\n');
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

QString StGenerator::fromFile(const QString& filePath)
{
    QFile f(filePath);
    if (!f.open(QFile::ReadOnly | QFile::Text)) {
        g_lastError = "Cannot open file: " + filePath;
        return {};
    }
    return fromXml(QString::fromUtf8(f.readAll()));
}

QString StGenerator::fromXml(const QString& xml)
{
    return doConvert(xml);
}

QString StGenerator::lastError()
{
    return g_lastError;
}
