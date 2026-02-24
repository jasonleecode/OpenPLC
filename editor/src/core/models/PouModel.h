#pragma once
#include <QString>
#include <QList>
#include "VariableDecl.h"

// POU（程序组织单元）类型
enum class PouType {
    Program,        // 程序
    FunctionBlock,  // 功能块
    Function,       // 函数
};

// 编程语言
enum class PouLanguage {
    LD,   // Ladder Diagram        梯形图
    ST,   // Structured Text       结构化文本
    IL,   // Instruction List      指令列表
    FBD,  // Function Block Diagram 功能块图
    SFC,  // Sequential Function Chart 顺序功能图
};

// 一个 POU 的完整数据
class PouModel {
public:
    PouModel(const QString& name, PouType type, PouLanguage lang);

    QString        name;
    PouType        pouType;
    PouLanguage    language;
    QString        description;
    QList<VariableDecl> variables;
    QString        code;          // ST/IL 的文本内容
    QString        graphicalXml; // LD/FBD/SFC 的 PLCopen XML 图形体（原始 XML 字符串）

    // ---- 枚举 ↔ 字符串转换（用于 XML） ----
    static QString    typeToString(PouType t);
    static PouType    typeFromString(const QString& s);
    static QString    langToString(PouLanguage l);
    static PouLanguage langFromString(const QString& s);

    // 返回标签页前缀，如 "LD"、"ST"
    static QString langTabPrefix(PouLanguage l);
};
