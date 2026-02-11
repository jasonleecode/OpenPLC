// 将 CircuitGraph 转为 C 代码

#pragma once
#include <QGraphicsScene>
#include <QString>
#include <QList>
#include <QMap>

class CodeGenerator {
public:
    // 唯一的对外接口：传入场景，吐出代码
    static QString generate(QGraphicsScene* scene);

private:
    // 辅助结构体：代表一个逻辑行
    struct Rung {
        QList<class ContactItem*> contacts;
        class CoilItem* coil = nullptr;
    };
};