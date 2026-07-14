# Editor 梯形图编译与下载链路状态

检查范围：`editor/` 中从梯形图项目保存、ST 生成、matiec 编译、driver 编译、产物生成，到 `DownloadDialog` 下载到 Runtime B 区的流程。

## 当前状态

已完成第一轮修复，并提交为：

```text
972fdf2 Fix editor ladder build and download pipeline
```

修复后，以下链路已经过验证：

- PLCopen/Beremiz `.tizi` 示例：`StGenerator -> iec2iec -> iec2c`
- PLCopen `traffic.tizi` SFC/FBD 混合示例：`StGenerator -> iec2iec -> iec2c -> Linux driver wrapper`
- TiZi 原生 `<TiZiProject>` 最小 LD 示例：`StGenerator -> iec2iec -> iec2c`
- TiZi 原生 LD 并联分支示例：多 `connectionPointIn`、falling edge、reset coil
- TiZi 原生 FBD block 示例：同一 `formalParameter` 多连接合成为布尔 `OR`
- TiZi 原生 LD 示例继续通过 Linux driver wrapper 编译为本机可执行文件
- Qt Editor 目标：`cmake --build editor/build --target TiZi --parallel 2`

可重复验证脚本：

```bash
editor/tests/verify_build_pipeline.sh
```

默认使用：

```text
QT_ROOT=$HOME/Qt/6.5.3/gcc_64
editor/tools/matiec_linux
```

如果 Qt 安装路径不同，可用：

```bash
QT_ROOT=/path/to/Qt/6.x/gcc_64 editor/tests/verify_build_pipeline.sh
```

## 已修复

### 1. 原生 `.tizi` 编译入口

原问题：`ProjectModel::saveTiZiNative()` 保存 `<TiZiProject>`，但 `StGenerator::fromXml()` 只接受 PLCopen `<project>`。

当前状态：

- `StGenerator` 已支持 `<TiZiProject>`。
- 原生项目会按 POU、变量、`graphical`/`code` 内容生成 IEC ST。
- 若存在 Program POU，会生成最小默认 `CONFIGURATION`。

### 2. LD 串联触点临时变量

原问题：串联触点生成 `_t1 := ...`，但没有声明 `_t1`。

当前状态：

- LD/FBD 转 ST 时会收集布尔临时变量。
- 生成 `TIZI_TMP* : BOOL;` 声明。
- 最小原生 LD fixture 已覆盖该路径。

### 3. LD 边沿触点与 Set/Reset 线圈

原问题：`PlcOpenViewer` 保存 `edge`/`storage`，但 `StGenerator` 忽略。

当前状态：

- contact `edge="rising"` / `edge="falling"` 会生成带状态记忆的脉冲逻辑。
- coil `storage="set"` / `storage="reset"` 会生成条件置位/复位。
- 相关状态变量会声明为 `TIZI_EDGE* : BOOL;`。

### 4. Build 产物与下载窗口衔接

原问题：Build 成功只打印产物路径，`DownloadDialog` 不知道最近一次产物。

当前状态：

- `MainWindow` 保存 `m_lastBuildOutput`。
- NCC post-build 后保存最终 `.bin` 或可执行产物路径。
- XCODE build 后保存 `.xcode.bin` 路径。
- `downloadProject()` 会预填最近一次构建产物。
- 下载前会校验文件格式、magic 和 B 区大小。

### 5. XCODE 下载镜像头

原问题：Runtime XCODE 需要 `[magic][wasm_size][wasm]`，Editor 只输出裸 `.wasm`。

当前状态：

- XCODE build 会生成 `*.xcode.bin`。
- 文件头使用小端 `XCODE_WASM_MAGIC = 0x57415300` 和 wasm size。
- 下载窗口默认选择该镜像，而不是裸 `.wasm`。

### 6. Linux matiec 工具路径

原问题：Linux 上 CMake 仍默认 `tools/matiec_mac`。

当前状态：

- CMake 按平台选择 `tools/matiec_linux` 或 `tools/matiec_mac`。
- Runtime 查找时要求 `iec2iec` 和 `iec2c` 都存在且可执行。
- 当前仓库已包含 `editor/tools/matiec_linux`。

### 7. B 区大小限制

原问题：LPC824 `max_size_bytes` 只显示，不阻止。

当前状态：

- NCC post-build 后会检查 `max_size_bytes`。
- 超限会 Build 失败，不再继续暴露为可下载产物。
- XCODE 镜像会按 driver `memory_map.user_flash_size_kb` 检查大小。
- `DownloadDialog` 下载前也会检查文件不超过 Runtime B 区 16KB。

### 8. 下载文件防误选

原问题：Build 后会预填正确产物，但用户仍可 Browse 任意文件。

当前状态：

- 只接受 NCC `.bin` 镜像或 XCODE `.xcode.bin` 镜像。
- NCC 镜像要求 offset 0 为 `USER_LOGIC_MAGIC = 0xDEADBEEF`。
- XCODE 镜像要求 offset 0 为 `XCODE_WASM_MAGIC = 0x57415300`，并校验 header 中的 wasm size 与文件长度一致。

### 9. LD 并联分支与多输入连接

原问题：`StGenerator` 解析 contact、coil、outVariable、inOutVariable 时只读取第一个 `<connection>`，并联支路或多个输入连接会被静默丢弃。

当前状态：

- 这些 LD/FBD 图元会保留所有 `connectionPointIn/connection`。
- 多个布尔输入会生成 `OR` 表达式。
- 新增 `native_ld_parallel_reset.tizi`，覆盖：
  - 左母线并联到两个触点
  - falling edge contact
  - 多输入 reset coil
  - `StGenerator -> iec2iec -> iec2c -> Linux driver wrapper`

### 10. Block 输入形式参数多连接

原问题：block 的每个输入变量只读取第一条 `<connection>`，如果同一 `formalParameter` 有多条输入来源，后续连接会被静默丢弃。

当前状态：

- block 输入会保留输入脚上的所有连接。
- 按目标 `formalParameter` 分组，同一参数的多个布尔来源会合成为 `OR` 表达式。
- `connection formalParameter="..."` 仍用于选择上游 block 的输出端口。
- 新增 `native_fbd_block_multi_input.tizi`，覆盖 `AND(IN1 := (A) OR (B), IN2 := C)` 并通过 `iec2iec -> iec2c -> Linux driver wrapper`。

### 11. Beremiz HMI_BOOL 类型兼容

原问题：`traffic.tizi` 的 `<dataTypes/>` 为空，但主程序变量使用 Beremiz/SVGHMI 扩展类型 `HMI_BOOL`，生成 ST 后 matiec 会在变量声明处报错。

当前状态：

- `HMI_BOOL` 会在 ST 生成时降级为 IEC 标准 `BOOL`。
- `traffic.tizi` 已加入 `verify_build_pipeline.sh`，覆盖 SFC/FBD 混合样例、标准库 FB、多输出端口引用和 Linux wrapper 编译。

## 剩余风险

### 1. 复杂 PLCopen LD/FBD 语义覆盖不足

当前 `StGenerator` 已覆盖基础路径，但仍不是完整 PLCopen 图形语言编译器。需要继续测试：

- 多输出线圈
- block 多输出端口在大型 PLCopen 样例中的完整回归
- 反馈回路
- `inOutVariable` 复杂引用
- 图元多 `connectionPointOut` 与端口选择

### 2. 边沿触点语义需要硬件/周期级确认

当前边沿逻辑按每次 POU 扫描更新 `TIZI_EDGE*`。这通过 matiec 语法和 C 生成验证，但还需要在真实扫描周期中确认与预期 PLC 行为一致。

### 3. XCODE 未做完整运行验证

已修复下载镜像格式，但还没有验证：

- WASI-SDK 实际编译
- `.xcode.bin` 下载到 Runtime B 区
- WAMR 加载镜像
- `plc_init()` / `plc_run(ms)` 实际调用

### 4. Linux matiec 二进制直接入库

当前方案能工作，但二进制入库会带来仓库体积和平台兼容风险。可选后续方案：

- 保留当前二进制，作为开发便利工具。
- 增加构建脚本，从 matiec 源码构建本机工具。
- 在 CI 中验证工具可执行。

### 5. 非 LD 图形编辑器仍有占位

Editor 中仍有部分功能是占位或未实现：

- Variable Browser
- License Editor
- Ethernet Runtime server side
- 部分非 LD 图形编辑 “coming soon”

这些不阻断当前 LD 编译下载链路，但影响产品完整性。

## 未提交的样例文件

当前工作区仍有一个用户已有修改：

```text
M editor/tests/first_steps/plcc.tizi
```

观察到的差异主要是 XML 属性顺序/格式被 Qt DOM 重写，新增 `TiZiBuild`，以及一处 `CounterLD` 中 block 位置从 `y="87"` 变为 `y="90"`。

建议单独确认该文件是否应作为样例更新提交；不要混入编译链路修复提交。
