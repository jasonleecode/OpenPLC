# TODO 与实现逐项复盘

复核日期：2026-10-29；代码基线：04f28a1。本文替代 2026-02-24 的旧架构审查作为当前事实依据，原文可从 Git 历史查阅。当前工作排序见 [todo.md](todo.md)。

> 2026-10-30 修复更新：下文保留 **04f28a1 基线的历史复现**。A/B/C 中的 P0-01～03 和 coil 连线/移动问题已修复并加入自动化回归，详见 [修复与验证记录](../docs/editor-persistence.md)。D 的编译类型语义、真实设备会话和 CI 环境仍是待办。请勿将下面的历史失败表当作当前版本测试结果。

## 结论与证据口径

旧清单明显落后于实现：主编译链、基础仿真、撤销栈、驱动安装、项目管理和 XML 函数库都已有代码。另一方面，“有代码”不能作为功能已完成的依据：本次在保存往返和图形连接中复现了现有编译回归未覆盖的问题。

当前应优先保证编辑结果被可靠保存、构建和运行，再完善真实设备会话及 I/O，最后做通用插件和多协议扩展。旧清单把已修复的吸附问题列 P1，却遗漏保存不一致；把全部在线调试列为不存在，也掩盖了仿真已有、真机未接通的区别。

证据分为：

- **实测**：运行当前代码的现有回归或最小复现。
- **源码确认**：有明确实现/调用证据，但未做交互或硬件验收。
- **待验证**：缺少相应设备、平台工具链或覆盖样例，不直接判定为代码故障。
- **规划**：扩展方向，不因另一个 IDE 有此功能就自动成为当前缺陷。

本次未执行硬件烧录、真实 I/O 控制或设备在线操作，未修改功能源代码。不会把本地协议替身或工具链检查当作真机验证。

## 原审查逐项核对

| 原条目 | 当前判断 | 实际证据与修正 | 对应待办 |
|---|---|---|---|
| 1.1 POU 没有类型区分、注释 | 部分过时 | PouModel 已有 PouType、description；缺少返回类型字段。原生函数生成固定 BOOL；描述字段存在也不等于所有导入导出路径均完整 | P0-01、P1-04 |
| 1.1 PUBLIC/PRIVATE/PROTECTED、保持属性 | 需拆分 | 访问修饰符属于语言范围规划；RETAIN 存在明确的输入语义丢失，不能混为“所有标准特性必须立刻补齐” | P1-04；高级特性 P3 |
| 1.2 变量模型不完整 | 仍成立，但应具体化 | VariableDecl 只有 name/class/type/initValue/comment；数组维度、地址和限定符无专用表示。PLCopen CONSTANT 已有部分生成支持，不能说所有限定符都没有 | P1-04 |
| 1.3 Task/Resource/Configuration 完全缺失、代码不能调度 | 结论过度 | 编辑模型/UI 缺失；StGenerator 已转换 PLCopen configuration/resource/task、全局变量，并为缺配置项目生成默认 10ms task；Linux wrapper 使用 common_ticktime__ | P2-01、P2-02 |
| 1.4 UDT 编辑器缺失 | 基本成立 | 未找到完整 UDT 编辑和 TYPE 声明生成；derived 类型名称能被读取，不代表定义已生成 | P2-05 |
| 2.1 导线移动后断开 | 问题仍存在，原因描述不完整 | PlcOpenViewer 已有 localId/端口形式参数连接表，不能只看 WireItem 的坐标字段；但移动未触发完整更新，另有 coil 入线未绘制 | P1-01 |
| 2.2 FBD 被梯级吸附 | 已过时 | BaseItem、FunctionBlockItem、VarBoxItem 拖动已使用 5 单位细网格，无旧的梯级中心强制吸附 | 从活动缺陷移除 |
| 2.3 缩放下吸附容差 | 仍成立 | 使用 scenePos 和固定 20/8 等场景距离，未按 view transform 换算；旧文称“固定像素”不准确 | P2-08 |
| 2.4 没有 Undo/Redo | 错误 | QUndoStack、Add/Delete/Move、导线端点/线段命令及 UI 接线均存在；但 undo 后序列化仍可能保留撤销项 | P0-02 |
| 2.5 图形不能保存 | 部分实现，仍有严重缺口 | toXmlString/buildBodyFromScene/ProjectManager 保存同步已有；不能说所有图形都丢失，也不能认定已完整解决。具体失败见下节 | P0-02 |
| 3.1 旧 C 生成器导致 Build 不可用 | 对当前主链不成立 | CodeGenerator 仍编入，但未找到业务调用；Build 使用 StGenerator。旧路径需要定位/清理，不是当前构建阻塞证据 | P3 |
| 3.2 StGenerator 是空文件，应改成 ST→matiec | 已完成基础路线 | StGenerator 约 1569 行；10 个现有输入案例通过 ST/matiec 验证 | 已有实现，继续补语义 |
| 3.3 只生成当前单个 POU | 错误，但多实例支持仍有限 | 两种格式均遍历多个 POU；原生默认只绑定首个 Program；仿真变量提取也只抓 resource1 首个实例，不应等同“所有多程序场景已支持” | P2-01、P2-04 |
| 4.1 连接是模拟的、没有通信协议 | 前半成立，后半过时 | Custom URI 假握手仍在；SerialTransport/TcpTransport/PlcProtocol/DownloadDialog 和 Runtime UART 协议已经存在 | P1-02 |
| 4.1 至少先实现 Modbus/HTTP | 优先级不合适 | 当前目标 Runtime 有自有协议，应先贯通已有命令/状态；采用其他协议需要目标设备依据 | 多协议 P3 |
| 4.2 完全没有在线监控基础设施 | 部分过时 | SmartSim/SimDebugSession/Debugger 有状态、单步、强制值、scan time、基础 trace；真实 PLC 变量通道、图形在线值和断点仍缺 | P1-02、P2-04 |
| 5.1 ProjectManager 空壳 | 错误 | 已被 MainWindow 实例化，负责新建、打开、保存和场景同步 | 保留，不可按旧清单删除 |
| 5.2 没有任何导入/导出，只能 .tizi | 过度概括 | 支持读取 PLCopen 与按来源格式保存；新建原生项目即使另存 .xml 仍是 TiZiProject。通用 PLCopen 导出和 CSV 未完整实现 | P0 保存先修，格式转换 P3 |
| 6.1 函数库只有静态树、不能拖放 | 部分过时 | library.xml 递归加载、搜索、拖放已实现；User-defined POU 节点仍未动态填充，第三方库插件未实现 | P2-05、P3 |
| 7/8 旧优先级和路线图 | 需要重排 | 去掉从零实现已存在模块；新增保存一致性、真实状态、CI、类型语义；配置模型与在线调试按现有基础拆分 | 以 todo.md 为准 |

主要源码：
[ProjectModel](../editor/src/core/models/ProjectModel.cpp)、
[PouModel](../editor/src/core/models/PouModel.h)、
[VariableDecl](../editor/src/core/models/VariableDecl.h)、
[StGenerator](../editor/src/core/compiler/StGenerator.cpp)、
[PlcOpenViewer](../editor/src/editor/scene/PlcOpenViewer.cpp)、
[UndoStack](../editor/src/utils/UndoStack.cpp)、
[MainWindow](../editor/src/app/MainWindow.cpp)、
[SmartSim](../editor/src/sim/smartsim.cpp)。

## 新确认的高优先级问题

### A. 导入项目的保存结果不等于内存模型

实测方法：创建最小 PLCopen XML，含 main Program 的 BOOL 变量 X 与 ST 代码，以及 old FunctionBlock；使用真实 ProjectModel::loadFromFile 加载，修改变量名、清空代码、删除 old、添加 added，saveToFile 后用新 ProjectModel 重开。

| 操作 | 预期 | 实际 |
|---|---|---|
| X 改名 CHANGED | 重开为 CHANGED | 仍为 X |
| 新增 added POU | 重开存在 | 不存在 |
| 删除 old POU | 重开不存在 | 恢复存在 |
| main 代码清空 | 重开为空 | 旧代码 X := TRUE; 仍在 |

源码原因：savePlcOpen 克隆原始 DOM，只按原 name 匹配已有 POU 更新 body；未重建 interface 和 POU 集合，且空 code 不进入文本写回分支。

影响不只是关闭后丢修改：Build 与仿真从保存文件读取输入，可能编译旧变量/旧程序。这比插件和新增语言能力更优先。

### B. 图形编辑、撤销与保存未闭环

在 Qt offscreen 环境直接调用真实 PlcOpenViewer 和 UndoStack（没有替换生产实现），实测：

| 路径 | 预期 | 实际 |
|---|---|---|
| initEmpty(FBD) 后 addFunctionBlock(TON) 并序列化 | block 具有端口定义 | 缺 inputVariables |
| 上一步撤销新增后序列化 | block 消失 | block 仍存在 |
| 导入 LD 后修改 contact 的 tagName | XML 写回新变量名 | 没写回 |
| 导入 LD 后 DeleteItemsCmd 删除 contact 再序列化 | 删除生效 | XML 仍有 contact |
| 导入画布后新增 TON | XML 包含 block | 没有写回 |
| 导入 contact→coil 图 | 显示一根连线 | 连线路径对象数量为 0 |
| 导入两个连接 contact，移动来源 contact 后处理事件 | wire path 更新 | path 未变 |

源码原因：

- 导入场景的 toXmlString 仅执行 syncPositionsToDoc/syncWirePathsToDoc，未同步属性和拓扑增删。
- AddItemCmd/DeleteItemsCmd 改 scene，m_items 注册表未同步；新建场景保存遍历注册表，撤销项仍在。
- buildBodyFromScene 对 block 只写基本属性，没有完整端口及按 formalParameter 关联连接。
- drawFbdWires 的目标分支包含 contact/outVariable/inOutVariable，却漏了 coil。
- 已有 updateAllWires 和连接表，但没有贯通普通元件移动的更新通知；不能将其描述为“完全没有逻辑引用”。

此处复现验证 API 行为和事件处理，不是完整鼠标 UI 回归。修复验收应同时覆盖鼠标编辑、undo/redo、保存重开、编译和运行。

### C. 变更状态与错误传播

源码确认，尚未做完整 UI 自动化：

- ST/IL textChanged 和变量表 cellChanged 只改 PouModel，不统一标记项目 dirty；图形 undo 栈也未统一接入 dirty 状态。
- ProjectManager::newProject 有 isDirty 检查，openProject(path) 没有同等检查；MainWindow 未实现统一关闭未保存确认。
- MainWindow::buildProject 与 SmartSim 下载路径忽略 saveToFile 返回值，之后仍可能读取旧文件。

需要统一修改检测、保存失败中止及新建/打开/退出确认，不能只修标题上的星号。

### D. 已暴露类型语义被丢失

使用当前 stgen_cli 与 matiec 的最小输入实测：

- PLCopen ARRAY [0..9] OF INT 被生成 ARRAY OF INT；matiec 返回 1，报告“no subrange list defined in array specification”。
- PLCopen 变量 address=%IX0.0 与 localVars retain=true 生成 ST 后均不再包含地址/RETAIN。
- 原生 function 即使带 returnType=INT，生成声明仍为 FUNCTION F : BOOL。

源码分别对应 StGenerator::itype、emitVarBlock、convertNativePou。对尚未支持的语义，至少明确拒绝或提示，不能静默生成不同含义的程序。将来支持 RETAIN 时需要明确目标存储策略，不应沿用旧文“必须写 NVS”的固定实现结论。

## 通信、运行时与工具链边界

### 连接与下载分开判断

- connectToPlc 的自定义 URI 分支仍用 singleShot(800) 显示连接成功；缺少真实握手、状态订阅和断线处理。
- PLC 菜单 Cold Start/Hot Start/Stop 只更新 UI 状态；PlcProtocol 已有 sendGetStatus/sendSetRun/sendReadIo，未见这些菜单调用。
- DownloadDialog 的串口/TCP 下载是真实实现，但与主窗口连接目标/状态未形成统一会话。
- Runtime 的 UART 接收、擦写、CRC、复位、状态、运行控制和 I/O 命令已有代码；不能说没有协议。
- TCP 回归使用 Python 客户端和 Python 设备替身，不直接运行 C++ PlcProtocol/TcpTransport，也不运行 MCU 的 runtime.c。
- CRC 校验不能替代在线固件/程序身份比较。原 TODO 的 MD5 需求需明确是 A 固件还是 B 程序，以及如何关联调试符号。

证据：[PlcProtocol](../editor/src/comm/PlcProtocol.h)、[DownloadDialog](../editor/src/comm/DownloadDialog.cpp)、[Runtime 协议](../runtime/app/runtime.c)、[TCP 测试](../editor/tests/verify_tcp_runtime_server.sh)。

### 物理 I/O 和调度

SystemAPI_t 已有 get_di/set_do；LPC824 的生成 wrapper 主要初始化 matiec、更新时钟和调用 config_run__，没有把地址变量映射到 API 的实现。不能用 SmartSim 手工 DI/DO 分配证明真机 I/O 已完成。

LPC824 wrapper 的 scan_ms 固定为 0，Runtime 默认 10ms；Linux wrapper 则读取 common_ticktime__。非 10ms 任务和多任务在目标板上的基准周期一致性仍需专项验证。本次没有实测板卡，不能断言所有任务调度错误，也不能声称完整支持。

证据：[LPC824 wrapper](../editor/tests/drivers/lpc824/templates/user_logic_wrapper.c)、[共享接口](../runtime/shared_interface.h)、[Runtime main](../runtime/app/main.c)、[Linux wrapper](../editor/tests/drivers/linux/templates/plc_main.c)。

### 驱动安装与 XCODE

- 驱动打包/安装和 driver.json 加载已有，不应重复立项开发。
- 安装器对 dir_name 未做安装根范围约束；子文件检查使用字符串 startsWith 而非目录边界；文件记录读取未逐次校验长度。此为源码确认，未执行越界写入复现。应完善路径、数据长度和安装失败处理。
- 默认 WASI-SDK 链接指向 /Users/jason/...，本机不可用。本次 XCODE 明确跳过。
- Linux WAMR 工具可用并通过测试，不能因此宣布实际 PLC wasm 编译或 MCU WAMR 路径已验收。
- XCODE 脚本分别启动 iwasm 调用 init/run，不是同一个实例的连续状态验证；后续应补同一实例的 PLC 行为测试。
- 不为复盘自动安装工具或烧录硬件；真实平台结果仍是待验证项。

## 验证记录与局限

| 验证 | 本次结果 | 覆盖边界 |
|---|---|---|
| 当前 Editor 构建/启动 | 本会话此前通过，提交 04f28a1 前增量构建也通过 | 不等于各编辑操作正确 |
| verify_build_pipeline.sh | PASS | 10 个 ST/matiec 输入案例；9 个 Linux driver 编译；边沿、反馈、多输出行为；含 Python TCP 替身测试 |
| verify_sim_pipeline.sh 默认样例 | PASS | 仿真变量、强制、单步、计数器等 |
| verify_sim_pipeline.sh traffic.tizi | PASS | SFC step/traffic 行为 |
| verify_toolchain_manifest.sh | PASS | 清单与路径检查；对软链接允许 lexists，不保证目标可用 |
| verify_wamr_linux.sh | PASS | Linux iwasm/库/header 工具验证 |
| verify_xcode_pipeline.sh | SKIP | 缺可执行 WASI-SDK clang；退出 0 不能记作真实编译通过 |
| 临时 Qt 保存/图形复现 | 上述失败现象已复现 | 直接使用生产类与 Qt offscreen；不是完整 GUI 自动化 |
| 数组/地址/保持/返回值最小转换 | 上述丢失与数组编译错误已复现 | 不代表所有类型用例均检查 |
| 干净 GitHub runner | 未运行，配置存在明确缺口 | workflow 未安装 Qt；脚本要求特定路径和 6.5.3 库文件。不能宣称已查看远端 CI 失败日志 |
| 真实硬件 / macOS / 真正 XCODE PLC 程序 | 未验证 | 后续独立验收 |

完整编译回归第一次在沙箱内因禁止 socket 创建而失败；获准在沙箱外重跑后通过，这是环境限制，不记录为项目协议故障。

复现材料保存在本次机器的 /tmp/openplc-todo-audit/：audit.cpp、CMakeLists.txt、check_codegen.py、results.txt、生成 XML/ST 和编译日志。它们是临时审查材料，不是已经纳入仓库的回归测试；关键操作与结果已记录在本文。现有回归日志位于 /tmp/openplc-audit-build.log、/tmp/openplc-audit-sim.log、/tmp/openplc-audit-traffic.log。

## 优先级调整理由

1. **保存和运行语义优先**：先消除编辑内容和实际运行程序不一致；不能被“编译通过”掩盖。
2. **复用现有基础**：修复导线与连接更新，接通现有协议、仿真和状态，补测试；不从零重做全部模块。
3. **区分模型、转换器和运行时**：配置能导入不等于能编辑；代码能生成不等于目标调度/I/O 已验证。
4. **区分缺陷与扩展**：丢失现有输入语义是缺陷，插件/新协议/高级语言特性是规划。
5. **真机目标影响顺序**：当前以编辑+仿真闭环排序；若交付真机，则物理 I/O、目标周期和下载验收提前。
6. **已实现不等于已验收**：驱动安装、保存、undo、trace 都应有具体剩余边界和验收标准。

旧文对 Beremiz 的具体文件行号、版本能力以及“IEC 合规必须支持全部功能”的断言未做外部核验，不再作为本项目优先级或完成度的依据。参考资料性质见 refer_editor.md / refer_runtime.md。
