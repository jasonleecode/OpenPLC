// CodeGenerator.cpp — FBD/LD 场景图 → C 代码生成器
#include "CodeGenerator.h"

#include "../../editor/items/ContactItem.h"
#include "../../editor/items/CoilItem.h"
#include "../../editor/items/FunctionBlockItem.h"
#include "../../editor/items/VarBoxItem.h"
#include "../../editor/items/WireItem.h"

#include <QList>
#include <QDateTime>
#include <QtMath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────
// 端口匹配容差（场景坐标，px）
// ─────────────────────────────────────────────────────────────
static constexpr qreal kTol = 8.0;

static qreal dist2(const QPointF& a, const QPointF& b)
{
    qreal dx = a.x() - b.x(), dy = a.y() - b.y();
    return dx*dx + dy*dy;
}

// ─────────────────────────────────────────────────────────────
// 在场景中找离 pt 最近（< kTol）的端口
// ─────────────────────────────────────────────────────────────
CodeGenerator::PortRef
CodeGenerator::findPort(QGraphicsScene* scene, const QPointF& pt)
{
    PortRef best;
    qreal   bestD = kTol * kTol;

    auto tryPort = [&](QGraphicsItem* item, int idx, bool isOut, QPointF portPt) {
        qreal d = dist2(pt, portPt);
        if (d < bestD) {
            bestD       = d;
            best.item   = item;
            best.index  = idx;
            best.isOutput = isOut;
        }
    };

    for (QGraphicsItem* gi : scene->items()) {
        if (auto* ct = dynamic_cast<ContactItem*>(gi)) {
            tryPort(gi, 0, false, ct->leftPort());
            tryPort(gi, 0, true,  ct->rightPort());
        }
        else if (auto* co = dynamic_cast<CoilItem*>(gi)) {
            tryPort(gi, 0, false, co->leftPort());
            tryPort(gi, 0, true,  co->rightPort());
        }
        else if (auto* fb = dynamic_cast<FunctionBlockItem*>(gi)) {
            for (int i = 0; i < fb->inputCount();  ++i)
                tryPort(gi, i, false, fb->inputPortPos(i));
            for (int i = 0; i < fb->outputCount(); ++i)
                tryPort(gi, i, true,  fb->outputPortPos(i));
        }
        else if (auto* vb = dynamic_cast<VarBoxItem*>(gi)) {
            tryPort(gi, 0, false, vb->leftPort());
            tryPort(gi, 0, true,  vb->rightPort());
        }
    }
    return best;
}

// ─────────────────────────────────────────────────────────────
// 扫描所有 WireItem，建立 conn 映射（输入端口 → 信号来源端口）
// ─────────────────────────────────────────────────────────────
void CodeGenerator::buildConnections(QGraphicsScene* scene, Ctx& ctx)
{
    for (QGraphicsItem* gi : scene->items()) {
        auto* wire = dynamic_cast<WireItem*>(gi);
        if (!wire) continue;

        PortRef a = findPort(scene, wire->startPos());
        PortRef b = findPort(scene, wire->endPos());

        if (!a.valid() || !b.valid()) continue;
        if (a.isOutput == b.isOutput)  continue;  // 两端同类型，忽略

        // 确保 src 是输出端口，dst 是输入端口
        PortRef src = a.isOutput ? a : b;
        PortRef dst = a.isOutput ? b : a;

        ctx.conn[dst] = src;
    }
}

// ─────────────────────────────────────────────────────────────
// 按 X 坐标从左到右排序场景中的有效图元（跳过 WireItem）
// ─────────────────────────────────────────────────────────────
QList<QGraphicsItem*> CodeGenerator::sortedItems(QGraphicsScene* scene)
{
    QList<QGraphicsItem*> result;
    for (QGraphicsItem* gi : scene->items()) {
        if (dynamic_cast<WireItem*>(gi))         continue;
        if (dynamic_cast<ContactItem*>(gi)     ||
            dynamic_cast<CoilItem*>(gi)        ||
            dynamic_cast<FunctionBlockItem*>(gi) ||
            dynamic_cast<VarBoxItem*>(gi))
            result.append(gi);
    }
    std::sort(result.begin(), result.end(), [](QGraphicsItem* a, QGraphicsItem* b) {
        return a->scenePos().x() < b->scenePos().x();
    });
    return result;
}

// ─────────────────────────────────────────────────────────────
// 为单个图元生成 C 代码行，写入 ctx
// ─────────────────────────────────────────────────────────────
void CodeGenerator::emitItem(QGraphicsItem* gi, Ctx& ctx)
{
    // ── ContactItem ──────────────────────────────────────────
    if (auto* ct = dynamic_cast<ContactItem*>(gi)) {
        PortRef inPort  { gi, 0, false };
        PortRef outPort { gi, 0, true  };

        QString inSig  = ctx.inputSig(inPort);  // 来自左侧的信号（无连线 = TRUE）
        if (inSig == "FALSE") inSig = "TRUE";   // 左端无连线 = 接电源
        QString varName = ct->tagName().isEmpty() ? "TRUE" : ct->tagName();

        QString expr;
        switch (ct->contactType()) {
        case ContactItem::NormalOpen:
            expr = (inSig == "TRUE")
                ? varName
                : QString("(%1 && %2)").arg(inSig, varName);
            break;
        case ContactItem::NormalClosed:
            expr = (inSig == "TRUE")
                ? QString("!%1").arg(varName)
                : QString("(%1 && !%2)").arg(inSig, varName);
            break;
        case ContactItem::PositiveTransition:
            expr = (inSig == "TRUE")
                ? QString("RISING_EDGE(%1)").arg(varName)
                : QString("(%1 && RISING_EDGE(%2))").arg(inSig, varName);
            break;
        case ContactItem::NegativeTransition:
            expr = (inSig == "TRUE")
                ? QString("FALLING_EDGE(%1)").arg(varName)
                : QString("(%1 && FALLING_EDGE(%2))").arg(inSig, varName);
            break;
        }

        QString sigName = ctx.newSig();
        ctx.sig[outPort] = sigName;
        ctx.body << QString("    bool %1 = %2;  // Contact [%3]")
                        .arg(sigName, expr, ct->tagName());
        return;
    }

    // ── CoilItem ─────────────────────────────────────────────
    if (auto* co = dynamic_cast<CoilItem*>(gi)) {
        PortRef inPort { gi, 0, false };
        QString inSig  = ctx.inputSig(inPort);
        if (inSig == "FALSE") inSig = "TRUE";
        QString var = co->tagName();

        switch (co->coilType()) {
        case CoilItem::Output:
            ctx.body << QString("    %1 = %2;  // Output coil").arg(var, inSig);
            break;
        case CoilItem::SetCoil:
            ctx.body << QString("    if (%1) %2 = true;  // Set coil").arg(inSig, var);
            break;
        case CoilItem::ResetCoil:
            ctx.body << QString("    if (%1) %2 = false;  // Reset coil").arg(inSig, var);
            break;
        case CoilItem::Negated:
            ctx.body << QString("    %1 = !%2;  // Negated coil").arg(var, inSig);
            break;
        }
        return;
    }

    // ── VarBoxItem (inVariable) ───────────────────────────────
    if (auto* vb = dynamic_cast<VarBoxItem*>(gi)) {
        PortRef outPort { gi, 0, true };
        PortRef inPort  { gi, 0, false };

        if (vb->role() == VarBoxItem::InVar) {
            // 输出信号 = 变量值（直接用表达式）
            ctx.sig[outPort] = vb->expression();
        }
        else if (vb->role() == VarBoxItem::OutVar) {
            // 输入信号 → 写入变量
            QString inSig = ctx.inputSig(inPort);
            ctx.body << QString("    %1 = %2;  // outVariable")
                            .arg(vb->expression(), inSig);
        }
        else { // InOutVar
            ctx.sig[outPort] = vb->expression();
            QString inSig = ctx.inputSig(inPort);
            if (inSig != "FALSE")
                ctx.body << QString("    %1 = %2;  // inOutVariable")
                                .arg(vb->expression(), inSig);
        }
        return;
    }

    // ── FunctionBlockItem ─────────────────────────────────────
    if (auto* fb = dynamic_cast<FunctionBlockItem*>(gi)) {
        const QString inst = fb->instanceName().isEmpty()
                             ? fb->blockType()
                             : fb->instanceName();
        const QString type = fb->blockType();

        // 全局实例声明（去重由调用方保证顺序，但一般一个 POU 不会重复 instanceName）
        ctx.globals << QString("%1_t %2;").arg(type, inst);
        ctx.body << "";
        ctx.body << QString("    // Function Block: %1 (%2)").arg(inst, type);

        // 写入各输入端口
        for (int i = 0; i < fb->inputCount(); ++i) {
            PortRef inPort { gi, i, false };
            QString inSig  = ctx.inputSig(inPort);
            if (inSig != "FALSE")
                ctx.body << QString("    %1.%2 = %3;")
                                .arg(inst, fb->inputPortName(i), inSig);
        }

        // 调用 FB 函数
        ctx.body << QString("    %1(&%2);").arg(type, inst);

        // 暴露各输出端口的信号
        for (int i = 0; i < fb->outputCount(); ++i) {
            PortRef outPort { gi, i, true };
            QString sigName = QString("%1.%2").arg(inst, fb->outputPortName(i));
            ctx.sig[outPort] = sigName;
        }
        return;
    }
}

// ─────────────────────────────────────────────────────────────
// 主入口
// ─────────────────────────────────────────────────────────────
QString CodeGenerator::generate(const QString& pouName, QGraphicsScene* scene)
{
    Ctx ctx;

    // 1. 建立连接图
    buildConnections(scene, ctx);

    // 2. 按 X 坐标顺序处理图元（数据流从左到右）
    QList<QGraphicsItem*> ordered = sortedItems(scene);
    for (QGraphicsItem* gi : ordered)
        emitItem(gi, ctx);

    // 3. 拼合输出 ─────────────────────────────────────────────
    QString out;
    out += QString("// Auto-generated by TiZi PLC Editor\n");
    out += QString("// POU: %1\n").arg(pouName);
    out += QString("// Date: %1\n\n").arg(
               QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    out += "#include <stdbool.h>\n";
    out += "#include \"plc_utils.h\"\n\n";

    // 全局 FB 实例（去重）
    QStringList uniqueGlobals;
    for (const QString& g : ctx.globals)
        if (!uniqueGlobals.contains(g)) uniqueGlobals << g;
    if (!uniqueGlobals.isEmpty()) {
        out += "// === Function Block instances ===\n";
        for (const QString& g : uniqueGlobals)
            out += g + "\n";
        out += "\n";
    }

    out += QString("void %1_run(void) {\n").arg(pouName);
    for (const QString& line : ctx.body)
        out += line + "\n";
    out += "}\n";

    return out;
}
