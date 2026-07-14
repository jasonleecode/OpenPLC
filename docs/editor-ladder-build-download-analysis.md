# Editor 梯形图编译与下载链路状态

检查范围：`editor/` 中从梯形图项目保存、ST 生成、matiec 编译、driver 编译、产物生成，到 `DownloadDialog` 下载到 Runtime B 区的流程。

## 当前结论

Editor 梯形图/功能块图/SFC 到可下载产物的主链路已经具备可重复验证能力。当前重点风险不再是基础编译链路，而是更复杂 PLCopen 图形语义和真实硬件/真实 WASI-SDK 环境验证。

当前已验证：

- PLCopen/Beremiz `.tizi` 示例：`StGenerator -> iec2iec -> iec2c`
- PLCopen `traffic.tizi` SFC/FBD/LD/action/transition 混合示例
- TiZi 原生 `<TiZiProject>` LD/FBD 示例
- LD rising/falling edge 的单扫描周期行为
- LD 并联、多输出、取反、set/reset 线圈
- FBD block 多输入、多输出端口、共享输出端口
- PLCopen FBD/LD 反馈回路运行行为
- Editor build 产物和下载窗口衔接
- NCC/XCODE 下载镜像格式校验
- TCP 下载协议桌面服务端回归
- Linux matiec 和 Linux WAMR 工具校验
- 轻量 CI 检查

默认本地工具：

```text
QT_ROOT=$HOME/Qt/6.5.3/gcc_64
editor/tools/matiec_linux
editor/tools/wasm_linux
```

Linux 上 `editor/tools/wasm/wasi-sdk` 保留给 macOS 默认工具路径；Linux XCODE 验证需要安装本机 WASI-SDK 并通过 `WASI_SDK_DIR` 指定。

## 验证入口

完整 Editor 编译链路回归：

```bash
editor/tests/verify_build_pipeline.sh
```

XCODE/WASM 构建链路验证：

```bash
WASI_SDK_DIR=/path/to/wasi-sdk editor/tests/verify_xcode_pipeline.sh
```

工具链和协议专项验证：

```bash
editor/tests/verify_matiec_tools.sh
editor/tests/verify_wamr_linux.sh
editor/tests/verify_toolchain_manifest.sh
editor/tests/verify_tcp_runtime_server.sh
```

Qt 安装路径不同可用：

```bash
QT_ROOT=/path/to/Qt/6.x/gcc_64 editor/tests/verify_build_pipeline.sh
```

## 覆盖矩阵

| 覆盖项 | 主要样例/脚本 | 当前状态 |
|---|---|---|
| PLCopen first steps | `editor/tests/first_steps/plc.tizi`、`plcc.tizi` | ST/matiec/C 编译通过 |
| PLCopen traffic SFC/FBD/LD | `editor/tests/first_steps/traffic.tizi` | SFC action、transition、TON/SR condition 覆盖 |
| TiZi 原生 LD | `native_ld*.tizi` | 串联、并联、edge、取反、set/reset 覆盖 |
| TiZi 原生 FBD | `native_fbd_block_multi_input.tizi` | block 同一输入多连接覆盖 |
| FBD 多输出/共享输出 | `native_fbd_fb_multi_output.tizi` | CTU `Q`/`CV`、共享 `Q` 运行回归覆盖 |
| PLCopen 反馈回路 | `plcopen_first_steps_linux` C driver | FBD/LD counter 连续扫描和 reset 覆盖 |
| SFC 多 actionBlock | `plcopen_sfc_multi_actionblock.tizi` | 同一步骤多个 actionBlock 合并覆盖 |
| 下载协议 | `verify_tcp_runtime_server.sh` | TiZi TCP byte protocol 回归覆盖 |
| 工具链 | `verify_*_tools.sh`、manifest | Linux matiec/WAMR 校验覆盖 |
| CI | `.github/workflows/editor-toolchain.yml` | 轻量工具链和协议检查 |

## 已修复问题

### 编译入口与基础生成

- `StGenerator` 支持 TiZi 原生 `<TiZiProject>`。
- 原生项目会按 POU、变量、`graphical`/`code` 内容生成 IEC ST。
- 若存在 Program POU，会生成最小默认 `CONFIGURATION`。
- Linux CMake 默认使用 `tools/matiec_linux`，macOS 使用 `tools/matiec_mac`。
- `verify_matiec_tools.sh` 会检查工具可执行性、平台二进制类型，并运行 smoke test。

### LD/FBD 图形语义

- LD/FBD 布尔临时变量会声明为 `TIZI_TMP* : BOOL;`。
- contact `edge="rising"` / `edge="falling"` 会生成带状态记忆的单扫描周期脉冲逻辑。
- coil `storage="set"` / `storage="reset"` 会生成条件置位/复位。
- contact、coil、outVariable、inOutVariable 保留所有 `connectionPointIn/connection`，多个布尔输入合成为 `OR`。
- block 同一 `formalParameter` 的多条输入连接会合成为 `OR`。
- `connection formalParameter="..."` 用于选择上游 block 输出端口。
- `outVariable negated`、`inOutVariable negatedIn/negatedOut` 已按 PLCopen 语义取反。
- FBD 功能块多输出端口已覆盖，例如 `CTU.Q` 和 `CTU.CV`。
- 同一功能块输出被多个下游共享时，不会重复调用功能块；运行测试覆盖 CTU `Q` 共享输出。

### PLCopen SFC/action/transition

- POU 级 `actions` 会生成 IEC `ACTION`，支持 ST/IL/LD/FBD/SFC body。
- SFC `actionBlock` 会保留 reference action 调用和 `S/R/D/P/N` qualifier，包括 duration 参数。
- 同一 step 的多个 `actionBlock` 会追加合并，不再互相覆盖。
- 命名 transition 的 ST/FBD/LD body 会展开为 SFC transition 条件表达式。
- SFC transition condition 直接连接无副作用布尔图形时，会解析 `inVariable`、`leftPowerRail`、`contact`、无实例 `NOT`/`AND`/`OR` block。
- SFC transition condition 连接 TON/SR 等有状态功能块时，会生成 `TIZI_SFC_TRANS* : BOOL` 和自动 `ACTION TIZI_TRANS*_COND`，在源 step 激活期间调用功能块并写入临时条件变量。
- `NOT` FBD block 会生成标准 `NOT (<expr>)`，避免 matiec 不接受 `NOT(IN := ...)`。

### 构建产物与下载

- Build 成功后 `MainWindow` 保存 `m_lastBuildOutput`。
- NCC post-build 后保存最终 `.bin` 或可执行产物路径。
- XCODE build 后保存 `.xcode.bin` 路径。
- `downloadProject()` 会预填最近一次构建产物。
- NCC 镜像要求 offset 0 为 `USER_LOGIC_MAGIC = 0xDEADBEEF`。
- XCODE 镜像要求 offset 0 为 `XCODE_WASM_MAGIC = 0x57415300`，并校验 header wasm size 与文件长度一致。
- NCC/XCODE 产物都会按 Runtime B 区大小限制检查。
- XCODE build 会生成 `[magic][wasm_size][wasm]` 格式的 `*.xcode.bin`，下载窗口默认选择该镜像。

### 工具链与 CI

- `verify_xcode_pipeline.sh` 覆盖 `StGenerator -> iec2c -> wasi-clang -> .wasm -> .xcode.bin`，可选调用 Linux `iwasm` 执行 `plc_init` / `plc_run`。
- `verify_wamr_linux.sh` 校验 Linux `iwasm 2.4.3`、WAMR 头文件和 `libiwasm.a`，并链接最小 C 程序。
- `editor/tools/toolchain_manifest.json` 记录工具类型、平台、版本、路径和验证脚本。
- `verify_toolchain_manifest.sh` 校验 manifest schema、唯一 ID、必需字段和路径存在性；对 macOS symlink 使用 `lexists`。
- `.github/workflows/editor-toolchain.yml` 覆盖 shell 语法、`git diff --check`、matiec、manifest、TCP runtime server、Linux WAMR 和 XCODE skip path。
- macOS `editor/tools/wasm` 目录保留；Linux 上通过 `WASI_SDK_DIR` / `WAMR_IWASM` 使用本机工具。

### TCP 下载协议

- `TcpTransport` 已通过 `QTcpSocket` 接入下载协议。
- 新增 `editor/tests/tools/tizi_tcp_runtime_server.py`，在桌面 Linux 上模拟 `runtime/app/runtime.c` 的 TiZi 字节帧协议。
- `verify_tcp_runtime_server.sh` 覆盖 `PING`、`ERASE`、`WRITE_PAGE`、`VERIFY`、`GET_STATUS`、`SET_RUN`、`READ_IO`、`RESET`。
- `verify_build_pipeline.sh` 会调用 TCP 协议测试，避免下载协议退化但编译测试仍通过。

### Editor 占位入口

- PLC Browser 菜单会打开只读变量浏览器，按 POU 汇总变量名、类型、作用域和初始值。
- License Editor 菜单改为 License Information，可查看项目元信息和仓库 LICENSE 内容。
- Ethernet 下载标签页文案已改为实际 TCP 传输说明。
- 未知 POU 语言兜底文案改为明确的 unsupported language；LD/FBD/SFC 仍走统一图形编辑器。

## 剩余风险

### 1. 复杂 PLCopen LD/FBD/SFC 语义

当前 `StGenerator` 已覆盖基础路径和多项复杂 PLCopen 图形路径，但仍不是完整 PLCopen 图形语言编译器。仍建议继续补：

- 更大型 PLCopen 样例完整回归。
- 更复杂反馈环路样例。
- 多 transition 共享同一有状态图形条件网络的调用顺序。
- 更多标准库功能块的端口组合和类型组合。

### 2. 真实硬件行为

边沿触点、下载复位、B 区切换、IO 刷新等路径已通过本机 driver 或协议替身覆盖基础语义，但仍需要在真实 Runtime 任务调度和真实 IO 刷新路径中确认。

### 3. XCODE/WAMR 真实运行

已有 XCODE 构建和镜像格式验证脚本，也已加入 Linux `iwasm` runtime 校验；但当前机器缺少可用 Linux WASI-SDK。安装工具链后仍需验证：

- `verify_xcode_pipeline.sh` 的真实 wasm 编译路径。
- `.xcode.bin` 下载到 Runtime B 区。
- WAMR/iwasm 加载镜像。
- `plc_init()` / `plc_run(ms)` 实际调用。

### 4. matiec 二进制交付方式

当前二进制入库方案能工作，并已有工具校验脚本；但仍存在仓库体积和平台兼容风险。可选后续方案：

- 保留当前二进制，作为开发便利工具。
- 增加构建脚本，从 matiec 源码构建本机工具。
- 在 CI 中验证或缓存工具 artifact。

### 5. Ethernet Runtime server side

Editor 端 TCP transport 已可用，桌面 TiZi TCP runtime server 已覆盖基础协议回归。真实 Runtime 侧仍需要在 MCU 网络栈中实现或确认 TCP server，把现有下载协议承载到 Ethernet，并在硬件上验证擦写、校验、复位与运行控制。
