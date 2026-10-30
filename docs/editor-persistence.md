# 编辑保存一致性修复与回归

日期：2026-10-30。对应 [TODO](../todo/todo.md) 的 P0-01、P0-02、P0-03，以及关联的 P1-01。历史复现见 [04f28a1 复盘](../todo/architectural-review.md)。

## 修复行为

- PLCopen 保存根据当前 POU/变量集合写回，支持新增、删除、名称修改、空 ST/IL 文本与描述；未编辑的数组定义、地址、保持属性、动作、配置及扩展 XML 保留。原生格式同时保存项目元数据和编译设置。
- `QSaveFile` 原子提交；无效图形 XML、打开或写入失败返回错误，旧文件和未保存状态保留。成功后才更新文件路径和保存状态。
- LD/FBD 使用活动图元注册表与逻辑连接序列化，覆盖导入和新建场景。新增、删除、属性修改、移动、导线折点与重连可保存；undo/redo 不再让已撤销图元残留或让删除图元复活。删除图元时处理相关导线，撤销保留原有 XML 扩展。
- 功能块保存输入/输出端口名称，按 `refLocalId` 和 `formalParameter` 写连接；标准函数不再自动带上 FB 实例名；导入实例名保留。新 ID 避开未渲染节点，未支持节点的 XML 保留。
- 补齐 coil 入线绘制，移动和撤销重做时端点跟随；未移动的导线保留自定义折点。
- 文本、变量、描述与图形编辑标记未保存；新建/打开/退出提供 Save/Discard/Cancel。保存失败或另存取消后，Build、打开 SmartSim 和仿真下载均停止。重开当前文件先完成保存再读取，项目切换清理场景与撤销栈。
- SFC 明确为只读图形预览，禁用图形修改入口并保留原始图形 XML，避免出现“能拖动但保存无效”。SFC 图形编辑仍属后续功能。

## 可重复验证

安装 Qt 6 的 Widgets、Svg、Xml、PrintSupport、SerialPort、Network、Test 开发组件，以及 CMake、C++/C 编译器。测试使用仓库内的 matiec；不会访问真实 PLC。

```bash
QT_ROOT=/path/to/Qt/6/gcc_64 editor/tests/verify_persistence.sh
```

脚本默认构建至 `/tmp/tizi-persistence-build`，可用 `TIZI_TEST_BUILD_DIR` 指定目录；未指定 `QT_ROOT` 时尝试 qmake 或 CMake 的 Qt 发现机制。已有构建目录也可直接使用：

```bash
cmake -S editor -B editor/build -DTIZI_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=/path/to/Qt/6/gcc_64
cmake --build editor/build --parallel 4
ctest --test-dir editor/build --output-on-failure -R '^editor_persistence$'
```

生产应用默认不构建测试。测试入口为 [editor_persistence_test.cpp](../editor/tests/editor_persistence_test.cpp)，直接链接生产模型、场景、撤销栈、项目管理与窗口实现，使用 Qt offscreen 和隔离设置目录。

| 测试组 | 覆盖 |
|---|---|
| projectRoundtrip | PLCopen 变量增删改、POU 增删/名称修改、清空文本、描述、重复保存；保留数组、地址、保持属性、动作及扩展 |
| atomicFailureAndNativeMetadata | 原生元数据/文本往返；无效 XML 和不可写入目标不覆盖旧文件、不清除 dirty |
| importedSceneHistory | 导入图元属性/移动/删除/恢复、导线联动、撤销分支丢弃、场景重置 |
| newSceneWireAndReconnection | 新建块端口、撤销新增、鼠标事件重连、多折点导线、保存重开 |
| namedFbdPortsAndUnexposedXml | 多输出端口 CV、反向画线、删除恢复、未渲染节点和扩展、ID 冲突防护、函数实例名、SFC 只读 |
| savedFixtureSemantics | fixtures 与 first_steps 共 10 个项目经场景保存重开后，生成的 ST 与保存前一致 |
| savedProgramExecutes | 修改导入 LD 的输入变量后保存，matiec + C 编译运行验证实际输出；新画 ADD FBD 保存编译运行得到 2 + 3 = 5 |
| unsavedChoices | 新建/打开时保存、放弃、取消、保存失败、另存取消及重开当前文件 |
| uiDirtyAndSaveFailure | 实际 MainWindow 中的文本/变量/图形 dirty；失败后 Build/仿真停止；退出取消/放弃；项目快速切换 |

2026-10-30 本地验证环境为 Linux + Qt 6.5.3：Editor 构建、上述 9 个测试组，以及以下已有回归通过：

```bash
bash editor/tests/verify_build_pipeline.sh
bash editor/tests/verify_sim_pipeline.sh
bash editor/tests/verify_sim_pipeline.sh editor/tests/first_steps/traffic.tizi
```

完整编译脚本包含本地 TCP 设备替身测试，需要允许绑定本地套接字。编译和仿真旧脚本的 Qt 发现方式仍待 P1-03 改进；新的 QtTest 尚未接入 CI。两次仿真脚本共用临时目录，应顺序执行。

## 验证边界

这是本机生产代码自动化验证，包含窗口/场景鼠标事件与实际生成程序执行，不代表真机下载、I/O 或全平台验收。保存保留 ARRAY/AT/RETAIN XML 不等于 StGenerator 已支持其编译语义，相关问题仍由 P1-04 跟踪。POU 名称保存与 XML 引用更新也不等于 ST/IL 文本中的标识符自动重构。尚未实现的 SFC 图形编辑、用户 POU 动态库、完整配置编辑仍在 TODO 中。
