# Editor 梯形图编译与下载链路检查

检查范围：`editor/` 中从梯形图项目保存、ST 生成、matiec 编译、driver 编译、产物生成，到 `DownloadDialog` 下载到 Runtime B 区的流程。

## 结论

当前链路存在多处实质问题。PLCopen/Beremiz 格式样例在部分环境下可能继续往后走，但 Editor 新建的原生 `.tizi` 梯形图项目、常见 LD 串联逻辑、XCODE 下载、以及编译后直接下载体验都存在断点。

## 主要问题

### 1. 新建/原生 `.tizi` 项目无法进入 ST 生成

位置：

- `editor/src/core/models/ProjectModel.cpp:88`
- `editor/src/core/models/ProjectModel.cpp:95`
- `editor/src/core/compiler/StGenerator.cpp:694`

`ProjectModel::saveToFile()` 对非 PLCopen 来源项目调用 `saveTiZiNative()`，保存根节点为 `<TiZiProject>`。但 `StGenerator::fromXml()` 只接受 PLCopen 根节点 `<project>`，否则直接返回 `Root element is not <project>`。

影响：

- 用户在 Editor 中新建梯形图项目并保存后，`Build` 会在第 1 步失败。
- 只有打开已有 PLCopen/Beremiz 风格文件时，才可能进入后续编译。

建议：

- 要么让 Build 阶段直接从 `ProjectModel` 生成 IEC ST，不再重新读取文件交给只支持 PLCopen 的 `StGenerator`。
- 要么给原生 `.tizi` 增加转换到 PLCopen/ST 的路径。
- 更彻底的做法是统一内部保存格式和编译输入格式，避免保存格式决定能否编译。

### 2. LD 串联触点生成未声明临时变量

位置：

- `editor/src/core/compiler/StGenerator.cpp:343`
- `editor/src/core/compiler/StGenerator.cpp:355`

LD/FBD 转 ST 时，串联触点会生成类似：

```iecst
_t1 := (A) AND B;
```

但 `_t1` 没有被加入 POU 的 `VAR ... END_VAR` 声明块。

影响：

- 两个或多个触点串联是最常见的梯形图结构，会导致 matiec 报未声明变量。

建议：

- 在 `fbdToSt()` 收集临时变量列表，并在 `convertPou()` 的变量声明区补充 `VAR _t1 : BOOL; ... END_VAR`。
- 或者尽量以内联表达式生成，避免产生需要声明的临时变量。

### 3. LD 边沿触点和置位/复位线圈语义丢失

位置：

- `editor/src/editor/scene/PlcOpenViewer.cpp:1403`
- `editor/src/editor/scene/PlcOpenViewer.cpp:1420`
- `editor/src/core/compiler/StGenerator.cpp:171`
- `editor/src/core/compiler/StGenerator.cpp:179`

`PlcOpenViewer::buildBodyFromScene()` 会保存：

- contact 的 `edge="rising"` / `edge="falling"`
- coil 的 `storage="set"` / `storage="reset"`

但 `StGenerator::parseFbd()` 只读取 `negated`，没有读取 `edge` 或 `storage`。生成 ST 时也只做普通触点和普通赋值线圈。

影响：

- 上升沿/下降沿触点会被当成普通触点。
- Set/Reset 线圈会被当成普通输出线圈。
- 编译可能成功，但运行行为不符合梯形图语义。

建议：

- 在 `Elem` 中增加 contact edge 和 coil storage 字段。
- 对边沿触点生成需要状态记忆的 ST 逻辑，或者映射到标准边沿功能块。
- 对 Set/Reset 线圈生成条件赋值：

```iecst
IF rung THEN
  Y := TRUE;
END_IF;
```

或 reset 对应 `FALSE`。

### 4. Build 成功产物没有传给下载窗口

位置：

- `editor/src/app/MainWindow.cpp:2095`
- `editor/src/app/MainWindow.cpp:2134`
- `editor/src/app/MainWindow.cpp:2144`
- `editor/src/comm/DownloadDialog.cpp:33`

`buildProject()` 计算了 `finalOutput` 并打印成功信息，但没有保存到 `MainWindow` 成员变量。`downloadProject()` 只是创建空的 `DownloadDialog`。虽然 `DownloadDialog::setBinaryPath()` 已存在，但没有被调用。

影响：

- 用户点击 Download 后需要手工 Browse。
- 容易选错旧文件、`.elf`、裸 `.wasm` 或其他非下载产物。

建议：

- 在 `MainWindow` 增加 `m_lastBuildOutput`。
- Build 成功后保存最终可下载文件路径。
- `downloadProject()` 中调用 `dlg.setBinaryPath(m_lastBuildOutput)`。
- 如果没有成功构建产物，提示先 Build。

### 5. XCODE 模式输出裸 `.wasm`，Runtime 需要带头部的 B 区镜像

位置：

- `editor/src/app/MainWindow.cpp:1886`
- `editor/src/app/MainWindow.cpp:1917`
- `editor/src/comm/PlcProtocol.cpp:58`
- `runtime/app/xcode_runner.c:63`

Runtime XCODE 模式期望 Flash B 起始布局：

```text
[4 bytes XCODE_WASM_MAGIC][4 bytes wasm_size][wasm bytes]
```

但 Editor XCODE 编译只生成裸 `.wasm`，下载协议也只是原样写入用户选择的文件内容。

影响：

- XCODE 下载后，Runtime 会检查 B 区头部魔数失败，认为没有合法 WASM。

建议：

- XCODE Build 后生成可下载镜像文件，例如 `plc_program.xcode.bin`。
- 文件内容为小端 `XCODE_WASM_MAGIC`、小端 wasm size、裸 wasm bytes。
- 下载窗口应默认选择这个镜像，而不是裸 `.wasm`。

### 6. 当前 Linux 工作区无法执行仓库内 matiec 工具

位置：

- `editor/CMakeLists.txt:122`
- `editor/tools/matiec_mac/iec2iec`
- `editor/tools/matiec_mac/iec2c`

CMake 固定：

```cmake
MATIEC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tools/matiec_mac"
```

但当前仓库内 `iec2iec` / `iec2c` 是 macOS arm64 Mach-O 可执行文件。

影响：

- 在 Linux 上，Editor 会“找到”工具文件，但执行会失败。

建议：

- 按平台选择 `matiec_linux` / `matiec_mac`。
- 或者让 `MATIEC_DIR` 成为 CMake cache 变量，由用户配置。
- Build 前检查工具是否可执行且平台匹配，失败时给明确错误。

### 7. LPC824 B 区 16KB 限制只显示，不阻止

位置：

- `editor/tests/drivers/lpc824/driver.json:46`
- `editor/src/app/MainWindow.cpp:2124`
- `editor/src/comm/PlcProtocol.cpp:62`

driver 中配置了：

```json
"max_size_bytes": 16384
```

Build 只打印大小，没有在超过限制时失败。`PlcProtocol::downloadBinary()` 会按 256 字节分页继续下载，直到 Runtime 端因为越过 B 区返回 NAK。

影响：

- 超限错误会延迟到下载中途出现。
- 用户无法在构建阶段得到明确反馈。

建议：

- post-build 后检查 `binFile` size。
- 若超过 `max_size_bytes`，Build 失败并禁止更新 `m_lastBuildOutput`。

## 优先修复顺序

1. 修复原生 `.tizi` 到 ST/PLCopen 的编译输入路径。
2. 修复 LD 临时变量声明，否则常见串联触点无法编译。
3. 修复 LD edge/storage 语义，否则编译成功也可能运行错误。
4. 保存 Build 最终产物路径并传给 `DownloadDialog`。
5. XCODE 生成带头部的下载镜像。
6. 平台化 matiec 工具路径。
7. 构建阶段强制检查 B 区大小限制。

## 备注

本次只做静态检查和轻量环境确认，没有修改业务代码。当前工作区中已有一个非本次产生的修改：

```text
M editor/tests/first_steps/plcc.tizi
```
