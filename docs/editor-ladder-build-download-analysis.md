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
- TiZi 原生 LD edge 示例：本机 C driver 验证 rising/falling edge 单扫描周期脉冲
- TiZi 原生 LD 并联分支示例：多 `connectionPointIn`、falling edge、reset coil
- TiZi 原生 LD 多输出示例：同一触点驱动多个线圈、变量输入/输出取反
- TiZi 原生 FBD block 示例：同一 `formalParameter` 多连接合成为布尔 `OR`
- TiZi 原生 LD 示例继续通过 Linux driver wrapper 编译为本机可执行文件
- Qt Editor 目标：`cmake --build editor/build --target TiZi --parallel 2`

可重复验证脚本：

```bash
editor/tests/verify_build_pipeline.sh
```

XCODE/WASM 构建链路验证脚本：

```bash
WASI_SDK_DIR=/path/to/wasi-sdk editor/tests/verify_xcode_pipeline.sh
```

matiec 工具链校验脚本：

```bash
editor/tests/verify_matiec_tools.sh
```

TCP 下载协议服务端测试：

```bash
editor/tests/verify_tcp_runtime_server.sh
```

当前仓库里的 `editor/tools/wasm/wasi-sdk` 是指向 macOS 下载目录的断链；Linux 上需要安装本机 WASI-SDK 并通过 `WASI_SDK_DIR` 指定。

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
- 新增 `verify_matiec_tools.sh`，检查工具可执行性、平台二进制类型，并运行 `iec2iec` / `iec2c` smoke test。

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

### 12. LD 多输出和变量取反

原问题：`outVariable negated` 以及 `inOutVariable negatedIn/negatedOut` 被解析链路忽略，相关图元在生成 ST 时不会按 PLCopen 语义取反。

当前状态：

- `outVariable negated="true"` 会生成取反赋值。
- `inOutVariable negatedIn="true"` 会在写入变量时取反。
- `inOutVariable negatedOut="true"` 会在作为上游信号输出时取反。
- 新增 `native_ld_multi_output_negation.tizi`，覆盖同一触点驱动多个线圈、取反线圈、`inOutVariable` 双向取反和 Linux wrapper 编译。

### 13. 边沿触点扫描周期语义

原风险：rising/falling edge 触点虽然能通过 matiec 编译，但没有确认生成 C 在连续扫描周期中的脉冲行为。

当前状态：

- 新增 `native_ld_edge_scan.tizi`。
- `verify_build_pipeline.sh` 会编译 matiec 输出的 C，并运行专用测试 driver。
- 测试覆盖：
  - 输入保持低电平时无 falling 误触发
  - 低到高只产生 1 个 rising 脉冲扫描周期
  - 高电平保持时无重复 rising 脉冲
  - 高到低只产生 1 个 falling 脉冲扫描周期
  - 低电平保持时无重复 falling 脉冲

### 14. XCODE 构建链路可验证化

原风险：XCODE 只修复了 `.xcode.bin` 镜像头，但没有独立脚本验证 `StGenerator -> iec2c -> wasi-clang -> .wasm -> .xcode.bin`。

当前状态：

- 新增 `verify_xcode_pipeline.sh`。
- 脚本会生成 ST、运行 `iec2c`、调用 WASI-SDK `clang` 生成 `.wasm`、生成 `.xcode.bin`，并校验：
  - XCODE magic `0x57415300`
  - header 中 wasm size 与实际 `.wasm` 一致
  - 镜像 payload 与 `.wasm` 完全一致
  - 如果 `llvm-objdump` 可用，检查 `plc_init` / `plc_run` 导出
- Editor 查找 WASI-SDK 时支持 `WASI_SDK_DIR` 环境变量，便于 Linux 本机或 CI 指定工具链路径。

### 15. Editor 占位入口清理

原问题：Editor 菜单和下载窗口中仍有多个用户可见的 “not implemented / coming soon” 占位入口。

当前状态：

- PLC Browser 菜单会打开只读变量浏览器，按 POU 汇总变量名、类型、作用域和初始值。
- License Editor 菜单改为 License Information，可查看项目元信息和仓库 LICENSE 内容。
- Ethernet 下载标签页文案已改为实际 TCP 传输说明；`TcpTransport` 已通过 `QTcpSocket` 接入下载协议。
- 未知 POU 语言的兜底文案改为明确的 unsupported language；LD/FBD/SFC 仍走统一图形编辑器。

### 16. Ethernet 下载协议服务端回归

原风险：Editor 端 TCP transport 已接入，但缺少服务端侧协议回归，后续改动可能破坏 TiZi 下载帧格式而不被测试发现。

当前状态：

- 新增 `editor/tests/tools/tizi_tcp_runtime_server.py`，在桌面 Linux 上模拟 `runtime/app/runtime.c` 使用的 TiZi 字节帧协议。
- 新增 `verify_tcp_runtime_server.sh`，覆盖 `PING`、`ERASE`、`WRITE_PAGE`、`VERIFY`、`GET_STATUS`、`SET_RUN`、`READ_IO`、`RESET`。
- `verify_build_pipeline.sh` 会调用该测试，确保编译链路验证时同时检查 TCP 下载协议基础语义。
- 该服务是桌面测试替身；真实 MCU Ethernet server 仍需要结合具体网络栈和硬件环境实现/验证。

## 剩余风险

### 1. 复杂 PLCopen LD/FBD 语义覆盖不足

当前 `StGenerator` 已覆盖基础路径，但仍不是完整 PLCopen 图形语言编译器。需要继续测试：

- block 多输出端口在大型 PLCopen 样例中的完整回归
- 反馈回路
- 图元多 `connectionPointOut` 与端口选择

### 2. 边沿触点真实硬件行为需要确认

当前边沿逻辑已经通过本机 C driver 的扫描周期回归，但还需要在真实 Runtime 任务调度和 IO 刷新路径中确认与预期 PLC 行为一致。

### 3. XCODE/WAMR 真实运行验证

已有 XCODE 构建和镜像格式验证脚本，但当前机器缺少可用 Linux WASI-SDK，仓库内 `wamrc` 也是 macOS arm64 可执行文件。仍需要在安装工具链后验证：

- `verify_xcode_pipeline.sh` 的真实 wasm 编译路径
- `.xcode.bin` 下载到 Runtime B 区
- WAMR/iwasm 加载镜像
- `plc_init()` / `plc_run(ms)` 实际调用

### 4. matiec 二进制交付方式

当前方案能工作，并已增加工具链校验脚本；但二进制入库仍会带来仓库体积和平台兼容风险。可选后续方案：

- 保留当前二进制，作为开发便利工具。
- 增加构建脚本，从 matiec 源码构建本机工具。
- 在 CI 中验证工具可执行。

### 5. Ethernet Runtime server side

Editor 端 TCP transport 已可用，桌面 TiZi TCP runtime server 已覆盖基础协议回归。真实 Runtime 侧仍需要在 MCU 网络栈中实现或确认 TCP server，把现有下载协议承载到 Ethernet，并在硬件上验证擦写、校验、复位与运行控制。

## 未提交的样例文件

当前工作区仍有一个用户已有修改：

```text
M editor/tests/first_steps/plcc.tizi
```

观察到的差异主要是 XML 属性顺序/格式被 Qt DOM 重写，新增 `TiZiBuild`，以及一处 `CounterLD` 中 block 位置从 `y="87"` 变为 `y="90"`。

建议单独确认该文件是否应作为样例更新提交；不要混入编译链路修复提交。
