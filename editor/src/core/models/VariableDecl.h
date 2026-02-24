#pragma once
#include <QString>

// 单个变量声明（对应变量表中的一行）
struct VariableDecl {
    QString name;       // 变量名，如 "Reset"
    QString varClass;   // 类型：Input / Output / InOut / Local / External
    QString type;       // 数据类型：BOOL / INT / REAL / DINT / WORD …
    QString initValue;  // 初始值（可空）
    QString comment;    // 注释（可空）
};
