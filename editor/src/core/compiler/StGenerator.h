#pragma once
#include <QString>

// ─────────────────────────────────────────────────────────────
// StGenerator — PLCopen XML → IEC 61131-3 Structured Text
//
// 将 Beremiz / OpenPLC 的 PLCopen XML（.tizi）格式转换为标准
// IEC 61131-3 ST 文本，供 matiec 编译器编译成 C 代码。
//
// 支持全部五种 IEC 语言：
//   ST  — 直接透传 CDATA
//   IL  — 直接透传 CDATA
//   FBD — 拓扑排序连接图 → ST 函数/功能块调用
//   LD  — 触点/线圈 + 功能块混合体 → ST（与 FBD 共用代码）
//   SFC — 步骤/转换/动作 → matiec 原生 SFC 文本
// ─────────────────────────────────────────────────────────────
class StGenerator {
public:
    /// 将 PLCopen XML 文件转换为 ST 文本
    static QString fromFile(const QString& xmlFilePath);

    /// 将 PLCopen XML 字符串转换为 ST 文本
    static QString fromXml(const QString& xmlContent);

    /// 最后一次调用的错误信息（为空表示成功）
    static QString lastError();
};
