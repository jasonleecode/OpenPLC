#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QDomDocument>
#include "PouModel.h"

// 整个 PLC 项目的数据容器
class ProjectModel : public QObject {
    Q_OBJECT
public:
    explicit ProjectModel(QObject* parent = nullptr);
    ~ProjectModel() override;

    QString projectName;   // 项目名称
    QString filePath;      // 保存路径（空 = 尚未保存）
    QList<PouModel*> pous; // 所有 POU 列表

    bool isDirty() const { return m_dirty; }
    void markDirty();
    void clearDirty();

    // POU 管理
    PouModel* addPou(const QString& name, PouType type, PouLanguage lang);
    void      removePou(const QString& name);
    PouModel* findPou(const QString& name) const;
    bool      pouNameExists(const QString& name) const;

    // XML 存档/读档
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);

    // 重置为空项目
    void clear();

signals:
    void changed();        // 数据有任何变动时发出
    void pouAdded(PouModel* pou);
    void pouRemoved(const QString& name);

private:
    bool         m_dirty           = false;
    QDomDocument m_sourcePlcOpen;             // 原始 PLCopen 文档（若从 PLCopen 加载）
    bool         m_isPlcOpenSource = false;   // 是否为 PLCopen 格式源文件

    // PLCopen XML 格式（Beremiz 兼容）导入
    bool loadPlcOpenXml(const QDomDocument& doc, const QString& path);
    // PLCopen XML 格式保存（Beremiz 兼容）
    bool savePlcOpen(const QString& path);
    // TiZi 自有格式保存
    bool saveTiZiNative(const QString& path);
    // 更新 ST/IL body 的 CDATA 内容
    static void updateStBody(QDomDocument& doc, QDomElement& pouElem, const QString& code);
};
