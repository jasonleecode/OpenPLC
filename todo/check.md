一、可以直接删除的死代码（无争议）

  ┌─────────────────────────────┬─────────────────────────────────────────────────────┐
  │            文件             │                        原因                         │
  ├─────────────────────────────┼─────────────────────────────────────────────────────┤
  │ core/models/CircuitGraph.h  │ 只有注释，无任何实现                                │
  ├─────────────────────────────┼─────────────────────────────────────────────────────┤
  │ core/models/LadderElement.h │ 同上                                                │
  ├─────────────────────────────┼─────────────────────────────────────────────────────┤
  │ core/models/Rung.h          │ 同上                                                │
  ├─────────────────────────────┼─────────────────────────────────────────────────────┤
  │ app/ProjectManager.cpp      │ 只有注释，项目管理功能已全部在 MainWindow 实现      │
  ├─────────────────────────────┼─────────────────────────────────────────────────────┤
  │ editor/items/RailItem.cpp   │ 只有注释，母线绘制在 LadderScene::drawBackground 里 │
  ├─────────────────────────────┼─────────────────────────────────────────────────────┤
  │ utils/XmlSerializer.cpp     │ 只有注释，XML 读写在 ProjectModel 里                │
  └─────────────────────────────┴─────────────────────────────────────────────────────┘

  这六个文件内容为零，删除毫无风险。

  > 回答：


  ---
  二、需要你澄清意图的文件

  1. editor/tools/device.cpp 和 drivers.cpp（两个空文件）

  这个 editor/tools/ 目录下放的是空壳，当初设计这两个文件的意图是什么？

  - device.cpp：是打算做"已连接设备管理"（类似设备配置、在线状态）？还是跟 comm/ 里的传输层重叠？
  - drivers.cpp：是打算做 driver 枚举/加载的逻辑层（现在散落在 MainWindow 里的 lambda）？

  我的疑问：现在 driver 的安装和枚举逻辑都内联在 MainWindow 的 lambda 里，如果 drivers.cpp
  的意图是把这部分抽出来，其实是合理的重构方向。但如果只是历史占位，直接删掉更干净。

  ---
  2. plugins/plugins.cpp（空文件）

  插件系统是未来规划还是已经放弃？TiZi 是否打算支持第三方扩展（比如自定义功能块库、自定义传输协议）？

  ---
  3. sim/smartsim.cpp（只有两行注释，提到"Smart Simulation Engine"）

  SmartSim 的定位是什么？
  - 软 PLC 仿真：在 PC 上离线运行 PLC 逻辑，用于调试（无需实体硬件）？
  - 在线监视：连接真实 PLC 后实时监视变量值？
  - 还是别的？

  这直接影响它在架构里的位置（属于 core/ 还是独立的 sim/）。

  ---
  1. CodeGenerator vs StGenerator 的职责边界

  - StGenerator：PLCopen XML → ST 文本（走 matiec 流水线，最终输出可执行文件）
  - CodeGenerator：直接从 LD 图形场景生成 C 代码（绕过 matiec）

  目前 buildProject() 调用的是 StGenerator 流水线，CodeGenerator 是否还在被调用？它的定位是什么——调试工具？还是并行的编译路径？

  ---
  三、结构性建议（供讨论，不一定要改）

  MainWindow 过大（约 2200 行）混杂了：
  1. UI 布局初始化（setupXxx 系列）
  2. 项目操作（new/open/save/build/download）
  3. POU 编辑器工厂（createPouEditorWidget 等）
  4. PLC 连接状态机
  5. 图标生成（makeLdIcon）
  6. 安装 driver 逻辑

  这些责任分散意味着改任何一处都要在同一个巨型文件里操作。是否考虑适度拆分？

  LadderView 的命名：它同时服务于 LadderScene 和 PlcOpenViewer，叫 GraphicsView 或 PlcView
  会更准确。但这是纯命名问题，改动成本低。