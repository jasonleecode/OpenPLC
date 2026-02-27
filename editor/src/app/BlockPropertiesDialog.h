#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QStringList>
#include <QWidget>

// ─────────────────────────────────────────────────────────────
// BlockPreviewWidget — 用 QPainter 实时绘制 FBD 功能块预览
// ─────────────────────────────────────────────────────────────
class BlockPreviewWidget : public QWidget
{
public:
    explicit BlockPreviewWidget(QWidget* parent = nullptr);

    void setBlock(const QString& typeName,
                  const QStringList& inNames,  const QStringList& inTypes,
                  const QStringList& outNames, const QStringList& outTypes,
                  const QString& instanceName = QString());

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString     m_name;
    QString     m_instanceName;
    QStringList m_inNames,  m_inTypes;
    QStringList m_outNames, m_outTypes;
};

// ─────────────────────────────────────────────────────────────
// BlockPropertiesDialog — Block Properties 对话框
//
//   左侧：名称 / 种类 / 描述 / 端口列表
//   右侧：FBD 块实时 Preview
// ─────────────────────────────────────────────────────────────
class BlockPropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    // instanceName: pass a non-null QString to show an editable name field
    // (with OK/Cancel). Pass a null QString (default) for read-only view (Close only).
    explicit BlockPropertiesDialog(
        const QString& name,
        const QString& kind,       // "function" / "functionBlock"
        const QString& comment,
        const QStringList& inNames,  const QStringList& inTypes,
        const QStringList& outNames, const QStringList& outTypes,
        QWidget* parent = nullptr,
        const QString& instanceName = QString());

    // Returns the edited instance name (only meaningful when instanceName was provided)
    QString getInstanceName() const;

private:
    QLineEdit* m_nameEdit = nullptr;
};
