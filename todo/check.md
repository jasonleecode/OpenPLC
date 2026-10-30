# 代码整理清单复核

日期：2026-10-29；基线：04f28a1。本文件替代旧的“六个文件可无争议删除”结论。功能排序见 [todo.md](todo.md)，证据见 [architectural-review.md](architectural-review.md)。

2026-10-30 更新：ProjectManager 的未保存确认/错误传播、ProjectModel 与图形保存一致性已完成 P0 修复。图形序列化已提取至 `editor/src/editor/scene/PlcOpenSerialization.cpp`，验证见 [回归记录](../docs/editor-persistence.md)；下表其余结构整理建议仍保留。

## 文件与职责

以下路径除特别说明外相对 editor/src。

| 旧记录 | 当前情况 | 建议 |
|---|---|---|
| core/models/CircuitGraph.h 空壳 | 仍为占位文本，无实现 | P3 清理候选；先检查引用 |
| core/models/LadderElement.h 空壳 | 仍为占位文本，无实现 | 同上 |
| core/models/Rung.h 空壳 | 仍为占位文本，无实现 | 同上 |
| app/ProjectManager.cpp 空壳，可删除 | **错误**：已实现新建/打开/保存/默认项目/场景同步，被 MainWindow 与 CMake 使用 | 保留；修复未保存提示和保存完整性 |
| editor/items/RailItem.cpp 空壳 | 仍为占位文本，未列入当前 CMake | 清理候选；不能推断母线功能不存在 |
| utils/XmlSerializer.cpp 空壳 | 仍为占位文本；序列化在 ProjectModel/PlcOpenViewer | 清理候选，实际保存缺陷另行修复 |
| editor/tools/device.cpp、drivers.cpp 为空 | 正确完整路径是 editor/src/editor/tools/，均为空 | 不阻断功能；提取设备/驱动服务时决定替代或删除 |
| plugins/plugins.cpp 为空 | 确实为空，未找到插件加载接口 | 保留插件规划；XML 库和 driver.json 不等于通用插件系统 |
| sim/smartsim.cpp 只有注释、定位待定 | **错误**：约 995 行，承担本机仿真构建、进程、I/O 和调试交互 | 保留；PC 仿真定位已明确，不能等同真机在线调试 |
| CodeGenerator / StGenerator 并行？ | 未找到 CodeGenerator::generate 业务调用；其头文件仍被包含、源文件仍被编译；实际 Build 使用 StGenerator | 旧路径清理候选；旧生成器问题不等于主编译链失效 |
| MainWindow 约 2200 行且包办项目管理 | 当前约 3503 行；项目生命周期已部分委托 ProjectManager，构建/UI/驱动/连接等仍集中 | 按功能边界逐步拆分，不先大重构 |
| LadderView 应重命名 | 命名偏好，非功能故障 | P3，不挤占保存、通信、测试 |

“未调用/未列入构建”来自本次静态搜索，不代表删除没有兼容成本。本次没有删除功能源文件。

## 优先厘清的边界

1. ProjectModel / PlcOpenViewer / UndoStack：统一编辑结果保存语义，避免场景和 XML 互不一致。
2. 连接会话：让 MainWindow 状态、DownloadDialog 和 PlcProtocol 使用明确的目标与设备回包。
3. 仿真符号表：编辑器已有 C++ generateSimVars，测试脚本仍有另一套生成逻辑；后续共享实现，避免二者偏离。
4. 构建与驱动服务：在修复工具链选择、安装验证、产物身份时提取，不机械拆文件。

旧清单中“ProjectModel 做文件 I/O 就是职责混乱”不再作为缺陷判据；关注保存正确性、错误传播和生命周期。
