# Editor PC 仿真设计

## 目标

`editor/src/sim` 的目标是在没有 PLC 真机时，为 editor 编译结果提供一个可在 PC 上运行、可观察、可干预的 PLC 仿真环境。仿真环境应复用现有梯形图/FBD/SFC 到 ST、ST 到 matiec C 的编译链路，避免在 editor 里维护另一套 PLC 语义解释器。

第一阶段先落地 SmartSim MVP：

- 将 `.tizi` 项目编译为 ST。
- 使用平台对应的 matiec 工具生成 C 代码。
- 链接生成代码、变量表和 PC 仿真运行时为本地进程。
- editor 通过 JSON Lines 控制该进程，实现启动、暂停、单步、变量读写、强制变量和释放强制。

## 当前实现

当前已加入基础仿真运行时：

- `editor/tools/sim_runtime/sim_api.h`
- `editor/tools/sim_runtime/sim_main.c`
- `editor/tests/verify_sim_pipeline.sh`

CI 已增加 `verify_sim_pipeline.sh`，覆盖从 fixture 项目到可运行仿真进程的闭环。

### 构建链路

当前验证脚本执行以下步骤：

1. 编译测试用 `stgen_cli`。
2. 使用 `StGenerator` 将 `.tizi` 项目导出为 ST。
3. 使用 `editor/tools/matiec_linux/iec2c` 生成 `POUS.h`、`resource1.c`、`config.c`。
4. 扫描 `POUS.h` 和 `resource1.c`，生成 `sim_vars.c`。
5. 将 `sim_main.c`、`sim_vars.c`、`config.c`、`resource1.c` 链接为本地仿真程序。
6. 启动仿真程序，通过 stdin/stdout JSON Lines 做功能验证。

后续 editor 内部的 `src/sim` 可以把第 1 到第 5 步封装为 `SimBuildService`，把第 6 步封装为 `SimSession`。

### 变量映射

`sim_vars.c` 暂时由构建侧生成，核心结构为：

```c
SimVar sim_vars[] = {
    {"main.CU", SIM_VAR_BOOL, &RESOURCE1__MAIN_INSTANCE.CU.value, 0, 0.0},
};
```

当前支持类型：

- `BOOL`
- `INT`
- `DINT`
- `REAL`
- `LREAL`

下一阶段应扩展到 PLCopen 常见类型，包括 `SINT`、`USINT`、`UINT`、`UDINT`、`LINT`、`ULINT`、`TIME`、`STRING`，并在 UI 层保留完整 IEC 类型信息。

## 运行时协议

仿真进程使用 stdin/stdout，每行一个 JSON 对象。所有响应都包含 `ok` 字段。

### 基础命令

```json
{"cmd":"hello"}
{"cmd":"init"}
{"cmd":"reset"}
{"cmd":"start","intervalMs":10}
{"cmd":"pause"}
{"cmd":"stop"}
{"cmd":"step"}
{"cmd":"status"}
```

`step` 会执行一次 PLC scan，并返回当前运行状态：

```json
{"ok":true,"running":false,"tick":1,"scanTimeUs":12,"intervalMs":10}
```

### 变量命令

读取全部变量：

```json
{"cmd":"readVars"}
```

读取单个变量：

```json
{"cmd":"readVars","name":"main.COUNT"}
```

写变量：

```json
{"cmd":"writeVar","name":"main.CU","value":true}
```

强制变量：

```json
{"cmd":"forceVar","name":"main.CU","value":true}
```

释放强制：

```json
{"cmd":"releaseForce","name":"main.CU"}
```

变量响应格式：

```json
{
  "ok": true,
  "vars": [
    {"name":"main.COUNT","type":"INT","value":5,"forced":false}
  ]
}
```

## editor/src/sim 建议模块

建议在 `editor/src/sim` 中按职责拆分：

- `SimBuildService`：接收当前项目路径和平台，生成 ST、调用 matiec、生成变量表、链接仿真程序。
- `SimVariableMap`：保存图形元素、ST 变量名、IEC 类型、运行时地址名之间的映射。
- `SimProcess`：封装仿真子进程、JSON Lines 编解码、超时、错误输出收集。
- `SimSession`：提供 `start`、`pause`、`step`、`reset`、`readVars`、`writeVar`、`forceVar` 等面向 UI 的 API。
- `SimDebugModel`：维护变量快照、强制状态、scan tick、scan time、断点/观察表达式等调试状态。

第一版 UI 不需要实现完整断点系统，可以先提供：

- 仿真构建/启动/停止。
- 单步 scan。
- 变量表实时刷新。
- 变量写入。
- force/release force。
- 运行状态、tick、scan time 展示。

## 后续重点

1. 将 `verify_sim_pipeline.sh` 中的变量表生成逻辑迁移为 editor 内部可复用实现。
2. 增加 macOS matiec/toolchain 选择，保留 Linux 和 macOS 双平台能力。
3. 扩展变量类型覆盖，尤其是无符号整数、`TIME` 和字符串。
4. 建立图形编辑器元素到运行时变量的稳定映射，支持在梯形图/FBD/SFC 上直接显示在线值。
5. 增加断点或条件暂停能力。MVP 可以先在每个 scan 后暂停并由 editor 判断条件。
6. 增加复杂 PLCopen LD/FBD/SFC fixture，覆盖 feedback loop、stateful FB、SFC action/transition、跨网络变量流。
7. 将运行时协议错误码结构化，便于 UI 显示可操作的错误原因。
