#include "CodeGenerator.h"
#include "../../editor/items/ContactItem.h"
#include "../../editor/items/CoilItem.h"
#include <algorithm> // 用于 std::sort

QString CodeGenerator::generate(QGraphicsScene* scene)
{
    // 1. 获取场景中所有图元
    QList<QGraphicsItem*> allItems = scene->items();
    
    // 2. 数据分类：按行 (Y坐标) 分组
    // Key: Y坐标 (int), Value: 这一行的所有元件
    QMap<int, Rung> logicRows;

    for (auto item : allItems) {
        // 判断是触点还是线圈
        // dynamic_cast 用于安全类型转换
        if (ContactItem* contact = dynamic_cast<ContactItem*>(item)) {
            int y = (int)contact->y();
            logicRows[y].contacts.append(contact);
        }
        else if (CoilItem* coil = dynamic_cast<CoilItem*>(item)) {
            int y = (int)coil->y();
            // 假设一行只有一个线圈
            logicRows[y].coil = coil;
        }
    }

    // 3. 开始生成 C 代码
    QString code;
    code += "// TiZi PLC Generated Code\n";
    code += "#include \"shared_interface.h\"\n\n";
    code += "void User_Loop(void) {\n";

    // 遍历每一行 (Map 会自动按 Key 也就是 Y坐标 排序)
    for (auto y : logicRows.keys()) {
        Rung& rung = logicRows[y];
        
        // 如果这一行没有线圈，跳过 (死逻辑)
        if (!rung.coil) continue;

        // 对触点按 X 坐标排序 (从左到右)
        std::sort(rung.contacts.begin(), rung.contacts.end(), 
            [](ContactItem* a, ContactItem* b) {
                return a->x() < b->x();
            });

        // 生成逻辑表达式: if (A && B && !C) ...
        QString expression;
        
        if (rung.contacts.isEmpty()) {
            expression = "1"; // 没有触点直接导通
        } else {
            for (int i = 0; i < rung.contacts.size(); ++i) {
                ContactItem* ct = rung.contacts[i];
                
                // 处理 AND 连接
                if (i > 0) expression += " && ";
                
                // 处理常开/常闭
                if (ct->tagName().startsWith("!")) { 
                    // 简单的取反逻辑演示
                    expression += QString("!Read_Bit(\"%1\")").arg(ct->tagName().mid(1));
                } else {
                    // 如果是常闭触点 (NC)
                    // 我们之前的 ContactItem 里有 Type，这里简单起见假设 NormalClosed 就要取反
                    // 实际上应该暴露 getter: if (ct->type() == NormalClosed) expression += "!";
                    // 这里假设 tagName 就是变量名
                    expression += QString("Read_Bit(\"%1\")").arg(ct->tagName());
                }
            }
        }

        // 写入代码
        code += QString("    // Rung at Y=%1\n").arg(y);
        code += QString("    if (%1) {\n").arg(expression);
        code += QString("        Write_Coil(\"%1\", 1);\n").arg(rung.coil->tagName());
        code += "    } else {\n";
        code += QString("        Write_Coil(\"%1\", 0);\n").arg(rung.coil->tagName());
        code += "    }\n\n";
    }

    code += "}\n";
    return code;
}