#pragma once
#include "BaseItem.h"
#include <QStringList>

class FunctionBlockItem : public BaseItem {
    Q_OBJECT
public:
    explicit FunctionBlockItem(const QString& blockType,
                               const QString& instanceName,
                               QGraphicsItem *parent = nullptr);

    QRectF  boundingRect() const override;
    void    paint(QPainter *painter,
                  const QStyleOptionGraphicsItem *option,
                  QWidget *widget) override;

    QPointF leftPort()  const override;
    QPointF rightPort() const override;

    QPointF inputPortPos(int i)  const;
    QPointF outputPortPos(int i) const;

    int inputCount()  const { return m_inputs.size(); }
    int outputCount() const { return m_outputs.size(); }

    // 按端口名查索引（用于 PLCopen 导入连线），未找到返回 -1
    int inputPortIndex(const QString& name) const {
        return m_inputs.indexOf(name);
    }
    int outputPortIndex(const QString& name) const {
        return m_outputs.indexOf(name);
    }

    // 按索引查端口名（用于代码生成）
    QString inputPortName(int i) const {
        return (i >= 0 && i < m_inputs.size()) ? m_inputs[i] : QString();
    }
    QString outputPortName(int i) const {
        return (i >= 0 && i < m_outputs.size()) ? m_outputs[i] : QString();
    }

    // 类型名和实例名（用于代码生成）
    QString blockType()    const { return m_blockType;    }
    QString instanceName() const { return m_instanceName; }

    void setBlockType(const QString& t);
    void setInstanceName(const QString& n);

    // 覆盖默认端口（供 PLCopen 导入使用）
    void setCustomPorts(const QStringList& inputs, const QStringList& outputs);

    void editProperties() override;

    enum { Type = UserType + 3 };
    int type() const override { return Type; }

    static const int BoxWidth  = 130;
    static const int HeaderH   = 44;
    static const int PortRowH  = 22;
    static const int PortLineW = 20;

protected:
    // FB 不参与梯级中心吸附（体积大，自由放置更合适），用网格吸附
    int portYOffset() const override { return 0; }

private:
    void rebuildPorts();
    int  boxHeight() const;

    QString     m_blockType;
    QString     m_instanceName;
    QStringList m_inputs;
    QStringList m_outputs;
};
