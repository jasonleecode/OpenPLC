# TiZi PLC Editor — 专业架构审查报告

> 以专业 PLC 系统架构师视角，对标 Beremiz 1.x
> 审查日期：2026-02-24

---

## 一、数据模型层（IEC 61131-3 合规性）

### 1.1 POU 模型不完整

```
当前：PouModel { name, language, graphicalXml, variables, bodyText }
```

IEC 61131-3 要求 POU 具备：

| 缺失字段 | 说明 |
|----------|------|
| POU 类型区分 | `PROGRAM` / `FUNCTION_BLOCK` / `FUNCTION` 语义完全不同 |
| 返回值类型 | FUNCTION 必须有返回值类型 |
| 访问修饰符 | `PUBLIC` / `PRIVATE` / `PROTECTED`（IEC 61131-3 ed.3） |
| `RETAIN` / `PERSISTENT` 属性 | 断电保持变量，运行时必须写 NVS |
| 注释 | POU 级文档注释 |

**Beremiz 做法**：`pouType`、`returnType`、`interface` 是独立的 XML 节点，模型层严格映射。

### 1.2 变量模型残缺

```cpp
// 当前 VariableDecl.h
struct VariableDecl { QString name, varClass, type, initialValue, comment; };
```

缺失：

| 缺失 | 影响 |
|------|------|
| 数组维度 `ARRAY [0..9] OF REAL` | 无法声明数组变量 |
| 结构体/枚举用户类型 | 无 `TYPE ... END_TYPE` 支持 |
| `AT %IX0.0` 地址绑定 | 无法映射物理 I/O |
| `RETAIN` / `CONSTANT` 限定符 | 语义丢失 |
| 初始值表达式 | 当前只存字符串，无类型验证 |

### 1.3 完全缺失：任务/资源/配置模型

Beremiz 的核心层次：

```
Configuration
  └── Resource
        ├── Task (CYCLIC/INTERRUPT/FREEWHEELING, interval, priority)
        └── POU instance 绑定 → Task
```

TiZi 目前没有：
- **Task**（周期、优先级、触发方式）
- **Resource**（CPU 核心/物理控制器映射）
- **Configuration**（全局变量声明区）
- **全局变量** (`VAR_GLOBAL`)

这意味着生成的代码永远无法正确调度，PLC 不知道 `plc_prg` 应该以什么周期运行。

### 1.4 完全缺失：用户数据类型（UDT）

没有 `TYPE ... END_TYPE` 编辑器，无法定义：
- `STRUCT` 结构体
- `ENUM` 枚举
- 子范围类型 `INT (0..100)`
- 数组别名

---

## 二、图形编辑器层

### 2.1 WireItem 位置耦合——致命缺陷

```cpp
// WireItem 当前存储的是绝对坐标
WireItem(QPointF start, QPointF end, ...);
```

**问题**：WireItem 不引用 `ContactItem*` / `FunctionBlockItem*`，只存两个坐标点。当用户移动任何元件时，导线端点不跟随移动，图形立刻断裂。

**Beremiz 做法**：导线存储 `(blockLocalId, formalParameter)` 逻辑引用，坐标仅用于渲染。移动块时重算导线路径。

**正确修复方向**：

```cpp
struct WireEndpoint {
    QGraphicsItem* item;   // 弱引用或 QPointer
    int portIndex;
    bool isOutput;
};
class WireItem {
    WireEndpoint m_src, m_dst;
    // 渲染时从 item 实时查询端口坐标
};
```

### 2.2 BaseItem 梯级吸附在 FBD 中触发

```cpp
// BaseItem::itemChange — 梯级吸附逻辑
if (scene()) {
    newPos.y() = RungBaseY + RungHeight * round(...);
}
```

FBD 中的功能块也继承 `BaseItem`，在 FBD 画布中拖动时会被强制吸附到梯级坐标，这在 FBD 中毫无意义。需要根据 `scene()` 类型或标志位禁用此逻辑。

### 2.3 端口吸附精度问题

`PlcOpenViewer` 的端口吸附搜索半径是固定像素值，当用户缩放视图时（`QGraphicsView::scale()`），屏幕像素和场景坐标的比例改变，导致吸附距离在放大时过大、缩小时失效。应将容差转换到场景坐标空间：

```cpp
qreal tol = 20.0 / view->transform().m11();  // 根据缩放换算
```

### 2.4 无撤销/重做系统

```
src/utils/UndoStack.h — 空文件，只有声明占位
```

所有编辑操作（放置元件、删除、移动、连线）都直接修改场景，没有 `QUndoCommand` 包装。工具栏上的 Undo/Redo 按钮目前是死按钮。

**最小修复**：引入 `QUndoStack`，将每次 `scene->addItem` / `scene->removeItem` 包装成 Command 对象。

### 2.5 场景序列化缺口

**新建 LD/FBD 程序 → 不能保存为 PLCopen XML**

- `ProjectModel::saveToFile` 只能保存 `bodyText`（ST/IL 文本）和 `graphicalXml`（从 PLCopen 导入的原始 XML）
- 用户在 `PlcOpenViewer` 中新建的图形元素（`ContactItem`、`CoilItem`、`FunctionBlockItem`、`WireItem`）没有序列化路径
- 关闭程序后，所有新建的梯形图内容丢失

**必须实现**：`PlcOpenViewer::toXmlString()` 方法，将场景中的图元序列化回 PLCopen XML 格式。

---

## 三、编译器层

### 3.1 生成的 C 代码无法编译

`CodeGenerator` 生成的代码存在根本性 API 错误：

```c
// 当前生成：
TON_t my_timer;
TON(&my_timer);   // ← 错误：OpenPLC Runtime 的 FB API 不是这样的
```

OpenPLC Runtime（基于 matiec）的正确调用方式：

```c
// matiec 生成的 C 风格
void TON_body__(TON *data__);
TON_body__(&my_timer);
```

而且生成代码缺少：
- PLC 扫描周期钩子（`__run()`）
- 全局变量访问（`%IX`、`%QX` 寄存器映射）
- FB 状态持久化（FB 实例必须是静态/全局的）
- `#include "POUS.h"` 等 matiec 运行时头文件

### 3.2 应生成 ST，再用 matiec 编译

正确的编译流水线（Beremiz 方式）：

```
图形程序 (LD/FBD) → ST 文本 → matiec → C 代码 → GCC → .so/.elf
```

TiZi 当前跳过 ST 直接生成 C，并且 `StGenerator.cpp` 是空文件。生成 ST 比生成 C 更容易且符合标准。

### 3.3 代码生成仅处理单个 POU

Beremiz 编译时处理：
1. 所有 POU（按依赖顺序排列）
2. 全局变量声明
3. Configuration/Resource/Task 绑定
4. I/O 映射表（`%IX0.0 → bool`）

TiZi 的 `buildProject()` 只取当前活跃 POU 的场景生成代码，无法构建完整可运行程序。

---

## 四、PLC 通信层

### 4.1 连接实现是模拟的

```cpp
// MainWindow.cpp — connectToPlc()
QTimer::singleShot(800, this, [this]() {
    setPlcConnState(PlcConnState::Connected);  // 假连接
});
```

没有实际的 PLC 通信协议实现。Beremiz 支持：
- **Modbus TCP/RTU**
- **EtherNet/IP**（原生）
- **PyRo**（Python 远程对象，用于 OpenPLC）
- **OPCUA**
- **ERPC**

至少应实现 **Modbus TCP** 或 **OpenPLC 的 HTTP REST API**。

### 4.2 完全缺失：在线监控

Beremiz 的在线模式功能：
- 变量监控窗口（实时值刷新）
- 强制/取消强制变量（Force/Unforce）
- 断点（Breakpoint）
- 单步扫描（Step Scan）
- 程序在线下载（Live Download）
- 扫描时间监控

TiZi 没有任何在线调试基础设施。

---

## 五、项目管理层

### 5.1 ProjectManager 是空壳

```
src/app/ProjectManager.cpp — 只有一行中文注释
```

`ProjectModel` 承担了本该由 `ProjectManager` 负责的文件 I/O，职责混乱。

### 5.2 没有导出/导入功能

Beremiz 支持：
- 导出 PLCopen XML（标准交换格式）
- 导出 `.po` 翻译文件
- 从 CSV 导入变量表

TiZi 只能保存自己的 `.tizi` 格式（本质上还是 PLCopen XML），没有与其他 IDE 互操作的导出路径。



---

## 六、函数库面板

### 6.1 静态树，没有实际库

```cpp
// MainWindow::setupLibraryPanel() — 当前实现
m_libraryTree->addTopLevelItem(...); // 手工添加几个固定节点
```

没有：
- 动态加载用户自定义 FB 库
- 从项目中的 FB 定义自动填充库节点
- 拖放放置功能块到画布

Beremiz 的库树是从 `stdlib.xml`、`extensionlib` 以及项目中的 POU 动态构建的，支持拖放。

---

## 七、优先级排序

| 优先级 | 问题 | 影响 |
|--------|------|------|
| 🔴 P0 | WireItem 不跟随元件移动 | 编辑器基本不可用 |
| 🔴 P0 | 新建图形程序无法保存 | 所有新建内容丢失 |
| 🔴 P0 | 无撤销/重做 | 用户操作无法回退 |
| 🟠 P1 | BaseItem 梯级吸附在 FBD 触发 | FBD 编辑错位 |
| 🟠 P1 | Task/Resource 模型缺失 | 生成代码无法调度 |
| 🟠 P1 | 编译器输出无法实际编译 | Build 功能无实用价值 |
| 🟡 P2 | 全局变量 / VAR_GLOBAL 支持 | 多 POU 程序无法共享数据 |
| 🟡 P2 | 在线监控基础设施 | 无法调试运行中的 PLC |
| 🟡 P2 | 实际 PLC 通信协议 | Connect 是假的 |
| 🟢 P3 | 用户数据类型 (UDT) | 复杂程序需要 |
| 🟢 P3 | AT 地址绑定 (%IX/%QX) | I/O 映射 |
| 🟢 P3 | 端口吸附缩放修正 | 精度问题 |

---

## 八、架构改进路线图

### 短期（P0 修复）

1. **实现 `PlcOpenViewer::toXmlString()`** — 解决保存问题
   - 遍历场景中的 ContactItem / CoilItem / FunctionBlockItem / VarBoxItem / WireItem
   - 序列化为 PLCopen XML `<body><LD>` 或 `<body><FBD>` 格式
   - 写回 `pou->graphicalXml`，纳入 `ProjectModel::saveToFile`

2. **WireItem 改为逻辑引用** — 解决移动断线
   - 存储 `QPointer<BaseItem> srcItem, dstItem` + 端口索引
   - `paint()` / `updatePosition()` 从 item 实时查询端口坐标
   - `itemChange` 通知机制：元件移动时调用 `wire->updatePosition()`

3. **集成 `QUndoStack`** — 撤销/重做
   - 在 `PlcOpenViewer` 中持有 `QUndoStack* m_undoStack`
   - 将 addItem / removeItem / moveItem 包装为 `QUndoCommand` 子类
   - 连接到工具栏 Undo/Redo 动作

### 中期（P1 修复）

4. **添加 Task/Resource/Configuration 数据模型**
   - `TaskModel { name, type, interval, priority }`
   - `ResourceModel { tasks, pouInstances }`
   - `ConfigModel { resources, globalVars }`
   - 在项目树中添加 "Configuration" 节点

5. **添加全局变量声明区**
   - `ProjectModel` 增加 `globalVars: QList<VariableDecl>`
   - 项目树 "Global Variables" 节点双击打开专用编辑器

6. **编译器改为生成 ST**
   - 实现 `StGenerator`：图形程序 → ST 文本
   - 对接 matiec（命令行调用）生成 C 代码
   - 或直接生成 matiec 兼容的 PLCopen XML，由 matiec 完整处理

### 长期（P2/P3）

7. **实现 Modbus TCP 在线监控**
8. **变量监控/强制面板**
9. **动态函数库加载**
10. **UDT 编辑器**（`TYPE ... END_TYPE`）
11. **AT 地址绑定**（`%IX0.0`、`%QX0.0`）
