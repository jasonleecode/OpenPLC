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
static QDomElement fc(const QDomElement& p, const QString& localName)
{
    for (QDomElement c = p.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        if (c.localName() == localName) return c;
    return {};
}

// 按本地名收集所有直接子元素
static QList<QDomElement> ch(const QDomElement& p, const QString& localName)
{
    QList<QDomElement> r;
    for (QDomElement c = p.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        if (c.localName() == localName) r << c;
    return r;
}

// 从 <ST>/<IL> 等语言元素内的 <xhtml:p> 提取 CDATA 文本
static QString cdata(const QDomElement& langEl)
{
    for (QDomElement c = langEl.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        if (c.localName() == "p") return c.text();
    return {};
}

// 从 <type> 元素提取 IEC 类型字符串
static QString itype(const QDomElement& typeEl)
{
    if (typeEl.isNull()) return "ANY";
    QDomElement child = typeEl.firstChildElement();
    if (child.isNull()) return "ANY";
    if (child.localName() == "derived") return child.attribute("name");
    if (child.localName() == "array") {
        QString base = itype(fc(child, "baseType"));
        return QString("ARRAY OF %1").arg(base);
    }
    return child.localName(); // BOOL INT REAL DINT WORD TIME …
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

    QList<Conn>    inputs;
    QList<QString> outputPorts;

    // 代码生成期间填充：输出端口名 → 已解析的 ST 信号表达式
    QMap<QString, QString> outSig;
};

// ───────────────────────────────────────────────────────────────────────────
// 解析 FBD/LD 体内的所有图元
// ───────────────────────────────────────────────────────────────────────────
static QMap<int, Elem> parseFbd(const QDomElement& bodyEl)
{
    QMap<int, Elem> map;
    for (QDomElement e = bodyEl.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement())
    {
        const QString tag = e.localName();
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
            QDomElement con = fc(fc(e, "connectionPointIn"), "connection");
            if (!con.isNull())
                el.inputs << Conn{con.attribute("refLocalId").toInt(),
                                  con.attribute("formalParameter"), {}};
        }
        else if (tag == "inOutVariable") {
            el.kind       = Elem::InOutVar;
            el.expression = fc(e, "expression").text().trimmed();
            QDomElement con = fc(fc(e, "connectionPointIn"), "connection");
            if (!con.isNull())
                el.inputs << Conn{con.attribute("refLocalId").toInt(),
                                  con.attribute("formalParameter"), {}};
        }
        else if (tag == "block") {
            el.kind         = Elem::Block;
            el.typeName     = e.attribute("typeName");
            el.instanceName = e.attribute("instanceName");

            QDomElement inVars = fc(e, "inputVariables");
            for (const QDomElement& v : ch(inVars, "variable")) {
                Conn c;
                c.param = v.attribute("formalParameter");
                QDomElement con = fc(fc(v, "connectionPointIn"), "connection");
                if (!con.isNull()) {
                    c.refId   = con.attribute("refLocalId").toInt();
                    c.refPort = con.attribute("formalParameter");
                }
                el.inputs << c;
            }
            QDomElement outVars = fc(e, "outputVariables");
            for (const QDomElement& v : ch(outVars, "variable"))
                el.outputPorts << v.attribute("formalParameter");
        }
        else if (tag == "contact") {
            el.kind       = Elem::Contact;
            el.expression = fc(e, "variable").text().trimmed();
            el.negated    = (e.attribute("negated") == "true");
            QDomElement con = fc(fc(e, "connectionPointIn"), "connection");
            if (!con.isNull())
                el.inputs << Conn{con.attribute("refLocalId").toInt(), {}, {}};
        }
        else if (tag == "coil") {
            el.kind       = Elem::Coil;
            el.expression = fc(e, "variable").text().trimmed();
            el.negated    = (e.attribute("negated") == "true");
            QDomElement con = fc(fc(e, "connectionPointIn"), "connection");
            if (!con.isNull())
                el.inputs << Conn{con.attribute("refLocalId").toInt(), {}, {}};
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
static QStringList fbdToSt(QMap<int, Elem>& elems)
{
    QStringList lines;
    int tmpN = 0;

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
            return src.expression;
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

    QList<int> order = topoSort(elems);

    for (int id : order) {
        Elem& el = elems[id];
        switch (el.kind) {
        case Elem::InVar:
        case Elem::PowerRail:
            break;

        case Elem::InOutVar: {
            if (!el.inputs.isEmpty() && el.inputs[0].refId >= 0) {
                QString s = sig(el.inputs[0].refId, el.inputs[0].refPort);
                if (!s.isEmpty() && s != el.expression)
                    lines << QString("  %1 := %2;").arg(el.expression, s);
            }
            break;
        }

        case Elem::Contact: {
            QString in = el.inputs.isEmpty() ? "TRUE"
                       : sig(el.inputs[0].refId, el.inputs[0].refPort);
            if (in.isEmpty()) in = "TRUE";
            QString varExpr = el.negated
                        ? QString("NOT %1").arg(el.expression)
                        : el.expression;
            if (in == "TRUE") {
                // 直接使用变量表达式，无需临时变量
                el.outSig[{}] = varExpr;
            } else {
                // 串联逻辑：需要 AND 表达式
                QString tmp = QString("_t%1").arg(++tmpN);
                el.outSig[{}] = tmp;
                lines << QString("  %1 := (%2) AND %3;").arg(tmp, in, varExpr);
            }
            break;
        }

        case Elem::Coil: {
            QString in = el.inputs.isEmpty() ? "FALSE"
                       : sig(el.inputs[0].refId, el.inputs[0].refPort);
            if (in.isEmpty()) in = "FALSE";
            QString val = el.negated ? QString("NOT (%1)").arg(in) : in;
            lines << QString("  %1 := %2;").arg(el.expression, val);
            break;
        }

        case Elem::Block: {
            QStringList args;
            for (const Conn& c : el.inputs) {
                if (c.refId < 0) continue;
                QString s = sig(c.refId, c.refPort);
                if (s.isEmpty()) s = "FALSE";
                if (!c.param.isEmpty())
                    args << QString("%1 := %2").arg(c.param, s);
                else
                    args << s;
            }
            if (el.instanceName.isEmpty()) {
                // 函数调用：检查输出是否被多次引用
                QString port = el.outputPorts.isEmpty() ? "OUT" : el.outputPorts.first();
                int uses = useCount.value({el.localId, {}}, 0)
                         + useCount.value({el.localId, port}, 0);
                QString callExpr = QString("%1(%2)").arg(el.typeName, args.join(", "));
                if (uses > 1) {
                    // 多次引用：需要临时变量
                    QString tmp = QString("_t%1").arg(++tmpN);
                    el.outSig[port] = tmp;
                    lines << QString("  %1 := %2;").arg(tmp, callExpr);
                } else {
                    // 单次引用：内联表达式（不生成赋值语句）
                    el.outSig[port] = callExpr;
                }
            } else {
                // 功能块调用（有状态，必须显式调用）
                lines << QString("  %1(%2);")
                         .arg(el.instanceName, args.join(", "));
                for (const QString& p : el.outputPorts)
                    el.outSig[p] = el.instanceName + "." + p;
            }
            break;
        }

        case Elem::OutVar: {
            if (!el.inputs.isEmpty()) {
                QString s = sig(el.inputs[0].refId, el.inputs[0].refPort);
                if (s.isEmpty()) s = "FALSE";
                lines << QString("  %1 := %2;").arg(el.expression, s);
            }
            break;
        }

        default: break;
        }
    }

    return lines;
}
// ───────────────────────────────────────────────────────────────────────────
// SFC → matiec 原生 SFC 文本
// ───────────────────────────────────────────────────────────────────────────
static QStringList sfcToText(const QDomElement& sfcEl)
{
    QStringList out;

    struct StepInfo {
        QString name;
        bool    initial = false;
    };
    QMap<int, StepInfo>      steps;
    QMap<int, QString>       transCond;  // localId → ST 条件
    QMap<int, QList<QString>> stepActs;  // stepLocalId → 内联 ST 列表

    // 解析所有节点
    for (QDomElement e = sfcEl.firstChildElement();
         !e.isNull(); e = e.nextSiblingElement())
    {
        QString tag = e.localName();
        int id = e.attribute("localId").toInt();

        if (tag == "step") {
            steps[id] = { e.attribute("name"),
                          e.attribute("initialStep") == "true" };
        }
        else if (tag == "transition") {
            QDomElement cond = fc(e, "condition");
            QDomElement inl  = fc(cond, "inline");
            QDomElement stEl = fc(inl, "ST");
            transCond[id] = cdata(stEl).trimmed();
        }
        else if (tag == "actionBlock") {
            QDomElement cpi = fc(e, "connectionPointIn");
            QDomElement con = fc(cpi, "connection");
            int stepId = con.isNull() ? -1 : con.attribute("refLocalId").toInt();
            QList<QString> acts;
            for (const QDomElement& act : ch(e, "action")) {
                QDomElement inl  = fc(act, "inline");
                QDomElement stEl = fc(inl, "ST");
                QString code = cdata(stEl).trimmed();
                if (!code.isEmpty()) acts << code;
            }
            if (stepId >= 0) stepActs[stepId] = acts;
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
        auto acts = stepActs.value(id);
        for (int i = 0; i < acts.size(); ++i)
            out << QString("  %1_act%2(N);").arg(s.name).arg(i);

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
    for (const auto& [id, s] : steps.asKeyValueRange()) {
        auto acts = stepActs.value(id);
        for (int i = 0; i < acts.size(); ++i) {
            out << QString("ACTION %1_act%2:").arg(s.name).arg(i);
            for (const QString& line : acts[i].split('\n'))
                out << "  " + line;
            out << "END_ACTION";
            out << "";
        }
    }

    return out;
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
    {
        QDomElement ev = fc(iface, "externalVars");
        bool isConst = ev.attribute("constant") == "true";
        emitVarBlock(ev, "VAR_EXTERNAL", isConst, out);
    }

    // ── 程序体 ────────────────────────────────────────────
    QDomElement body = fc(pouEl, "body");

    // ST
    QDomElement stEl = fc(body, "ST");
    if (!stEl.isNull()) {
        QString code = cdata(stEl);
        for (const QString& ln : code.split('\n'))
            out << "  " + ln;
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
        out << endKeyword;
        out << "";
        return out;
    }

    // SFC
    QDomElement sfcEl = fc(body, "SFC");
    if (!sfcEl.isNull()) {
        for (const QString& ln : sfcToText(sfcEl))
            out << "  " + ln;
        out << endKeyword;
        out << "";
        return out;
    }

    // FBD / LD（统一处理）
    QDomElement fbdEl = fc(body, "FBD");
    if (fbdEl.isNull()) fbdEl = fc(body, "LD");
    if (!fbdEl.isNull()) {
        auto elems = parseFbd(fbdEl);
        for (const QString& ln : fbdToSt(elems))
            out << ln;
        out << endKeyword;
        out << "";
        return out;
    }

    out << "  (* Unsupported body language *)";
    out << endKeyword;
    out << "";
    return out;
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
    if (root.localName() != "project") {
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
