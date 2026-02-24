// CodeGenerator — FBD/LD 场景图 → C 代码
// 通过导线连接图进行拓扑排序，逐项生成代码
#pragma once

#include <QGraphicsScene>
#include <QString>
#include <QList>
#include <QMap>

class QGraphicsItem;

// ─────────────────────────────────────────────────────────────
// CodeGenerator
//   generate(pouName, scene) 是唯一入口。
//   它会：
//     1. 扫描场景中的 WireItem，将端点匹配到各图元的端口
//     2. 建立 连接图：输出端口 → 输入端口
//     3. 按数据流（X 坐标）顺序处理图元
//     4. 生成 <pouName>_run() C 函数
// ─────────────────────────────────────────────────────────────
class CodeGenerator {
public:
    static QString generate(const QString& pouName, QGraphicsScene* scene);

private:
    // ── 端口标识 ──────────────────────────────────────────────
    struct PortRef {
        QGraphicsItem* item     = nullptr;
        int            index    = 0;       // FunctionBlockItem 时为端口序号，其余为 0
        bool           isOutput = false;

        bool operator<(const PortRef& o) const {
            if (item    != o.item)    return item    < o.item;
            if (index   != o.index)   return index   < o.index;
            return isOutput < o.isOutput;
        }
        bool operator==(const PortRef& o) const {
            return item == o.item && index == o.index && isOutput == o.isOutput;
        }
        bool valid() const { return item != nullptr; }
    };

    // ── 编译上下文 ─────────────────────────────────────────────
    struct Ctx {
        // 连接关系：输入端口 → 其信号来自哪个输出端口
        QMap<PortRef, PortRef>  conn;
        // 每个输出端口已计算的 C 信号表达式
        QMap<PortRef, QString>  sig;
        // 全局声明（FB 实例、中间变量等）
        QStringList             globals;
        // 函数体行
        QStringList             body;
        // 中间变量计数
        int                     sigCnt = 0;

        QString newSig() { return QString("_s%1").arg(++sigCnt); }
        // 获取输入端口对应的信号字符串（未连接 → "FALSE"）
        QString inputSig(const PortRef& inPort) const {
            auto it = conn.find(inPort);
            if (it == conn.end()) return "FALSE";
            return sig.value(it.value(), "FALSE");
        }
    };

    // ── 私有方法 ──────────────────────────────────────────────
    // 扫描 WireItem，构建 Ctx.conn
    static void   buildConnections(QGraphicsScene* scene, Ctx& ctx);
    // 在容差范围内查找 pt 对应的端口
    static PortRef findPort(QGraphicsScene* scene, const QPointF& pt);
    // 按 X 顺序对图元排序（源节点在左）
    static QList<QGraphicsItem*> sortedItems(QGraphicsScene* scene);
    // 为单个图元生成代码，写入 ctx.body / ctx.globals
    static void   emitItem(QGraphicsItem* item, Ctx& ctx);
};
