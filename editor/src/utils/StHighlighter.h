#pragma once
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QVector>

// IEC 61131-3 Structured Text / Instruction List 语法高亮器
class StHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit StHighlighter(QTextDocument* parent = nullptr)
        : QSyntaxHighlighter(parent)
    {
        // 1. 关键字（控制流 + 程序单元）
        QTextCharFormat kwFmt;
        kwFmt.setForeground(QColor("#0000CC"));
        kwFmt.setFontWeight(QFont::Bold);

        const QStringList keywords = {
            "IF", "THEN", "ELSE", "ELSIF", "END_IF",
            "WHILE", "DO", "END_WHILE",
            "FOR", "TO", "BY", "END_FOR",
            "REPEAT", "UNTIL", "END_REPEAT",
            "CASE", "OF", "END_CASE",
            "FUNCTION", "END_FUNCTION",
            "FUNCTION_BLOCK", "END_FUNCTION_BLOCK",
            "PROGRAM", "END_PROGRAM",
            "VAR", "END_VAR",
            "VAR_INPUT", "VAR_OUTPUT", "VAR_IN_OUT",
            "VAR_GLOBAL", "VAR_EXTERNAL",
            "RETURN", "EXIT",
            "NOT", "AND", "OR", "XOR", "MOD",
        };
        for (const QString& kw : keywords) {
            HighlightRule r;
            r.pattern = QRegularExpression(
                QString("\\b%1\\b").arg(kw),
                QRegularExpression::CaseInsensitiveOption);
            r.format = kwFmt;
            m_rules.append(r);
        }

        // 2. 数据类型
        QTextCharFormat typeFmt;
        typeFmt.setForeground(QColor("#007070"));
        typeFmt.setFontWeight(QFont::Bold);

        const QStringList types = {
            "BOOL", "BYTE", "WORD", "DWORD", "LWORD",
            "SINT", "USINT", "INT", "UINT", "DINT", "UDINT", "LINT", "ULINT",
            "REAL", "LREAL",
            "TIME", "DATE", "TIME_OF_DAY", "TOD", "DATE_AND_TIME", "DT",
            "STRING", "WSTRING", "CHAR", "WCHAR",
        };
        for (const QString& t : types) {
            HighlightRule r;
            r.pattern = QRegularExpression(
                QString("\\b%1\\b").arg(t),
                QRegularExpression::CaseInsensitiveOption);
            r.format = typeFmt;
            m_rules.append(r);
        }

        // 3. 布尔常量
        QTextCharFormat constFmt;
        constFmt.setForeground(QColor("#990066"));
        constFmt.setFontWeight(QFont::Bold);
        {
            HighlightRule r;
            r.pattern = QRegularExpression(
                "\\b(TRUE|FALSE)\\b",
                QRegularExpression::CaseInsensitiveOption);
            r.format = constFmt;
            m_rules.append(r);
        }

        // 4. 数字字面量（整数、浮点数、十六进制）
        QTextCharFormat numFmt;
        numFmt.setForeground(QColor("#116611"));
        {
            HighlightRule r;
            r.pattern = QRegularExpression(
                "\\b(16#[0-9A-Fa-f]+|8#[0-7]+|2#[01]+|\\d+\\.\\d*([Ee][+-]?\\d+)?|\\d+)\\b");
            r.format = numFmt;
            m_rules.append(r);
        }

        // 5. 字符串字面量 '...'
        QTextCharFormat strFmt;
        strFmt.setForeground(QColor("#AA3300"));
        {
            HighlightRule r;
            r.pattern = QRegularExpression("'[^']*'");
            r.format = strFmt;
            m_rules.append(r);
        }

        // 6. 行注释 // ...
        QTextCharFormat lineCommentFmt;
        lineCommentFmt.setForeground(QColor("#777777"));
        lineCommentFmt.setFontItalic(true);
        {
            HighlightRule r;
            r.pattern = QRegularExpression("//[^\n]*");
            r.format = lineCommentFmt;
            m_rules.append(r);
        }

        // 7. 块注释 (* ... *)  —— 跨行，用 QSyntaxHighlighter 多状态机制
        m_blockCommentFmt.setForeground(QColor("#777777"));
        m_blockCommentFmt.setFontItalic(true);
        m_commentStart = QRegularExpression("\\(\\*");
        m_commentEnd   = QRegularExpression("\\*\\)");
    }

protected:
    void highlightBlock(const QString& text) override
    {
        // —— 单行规则 ——
        for (const HighlightRule& r : m_rules) {
            QRegularExpressionMatchIterator it = r.pattern.globalMatch(text);
            while (it.hasNext()) {
                QRegularExpressionMatch m = it.next();
                setFormat(m.capturedStart(), m.capturedLength(), r.format);
            }
        }

        // —— 跨行块注释 (* ... *) ——
        setCurrentBlockState(0);

        int startIdx = 0;
        if (previousBlockState() != 1) {
            // 本行不在块注释中，找第一个 (*
            QRegularExpressionMatch m = m_commentStart.match(text);
            startIdx = m.hasMatch() ? m.capturedStart() : -1;
        }

        while (startIdx >= 0) {
            QRegularExpressionMatch endMatch = m_commentEnd.match(text, startIdx);
            int endIdx, commentLen;
            if (endMatch.hasMatch()) {
                endIdx     = endMatch.capturedStart();
                commentLen = endIdx - startIdx + endMatch.capturedLength();
                setCurrentBlockState(0);
            } else {
                setCurrentBlockState(1);        // 块注释延续到下一行
                commentLen = text.length() - startIdx;
            }
            setFormat(startIdx, commentLen, m_blockCommentFmt);

            if (endMatch.hasMatch()) {
                // 找下一个 (*
                QRegularExpressionMatch nextStart =
                    m_commentStart.match(text, startIdx + commentLen);
                startIdx = nextStart.hasMatch() ? nextStart.capturedStart() : -1;
            } else {
                break;
            }
        }
    }

private:
    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat    format;
    };

    QVector<HighlightRule>  m_rules;
    QTextCharFormat         m_blockCommentFmt;
    QRegularExpression      m_commentStart;
    QRegularExpression      m_commentEnd;
};
