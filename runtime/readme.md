# TiZi PLC Runtime

基于 NXP LPC824（ARM Cortex-M0+）的裸机 PLC 执行核心。采用 A/B 双区 Flash 架构，支持通过串口从上位机（TiZi Editor）在线更新用户 PLC 程序。

---

## 硬件平台

| 项目 | 参数 |
|------|------|
| MCU | NXP LPC824M201JDH20 |
| 内核 | ARM Cortex-M0+ @ 30MHz |
| Flash | 32KB（分 A/B 两区，各 16KB）|
| SRAM | 8KB（分 Runtime / UserLogic 各 4KB）|
| 通信 | UART（下载协议 + 调试输出）|
| I/O | DI × 4（PIO0_16~19）、DO × 4（PIO0_12~15）|

---

## A/B 双区架构

```
Flash 地址映射：
┌─────────────────────────────────┐
│  0x00000000  Runtime A  16KB    │  ← 固件本体，通常只烧录一次
│  (扇区 0-15)                    │     ARM 向量表 + BSP + PLC 调度核心
├─────────────────────────────────┤
│  0x00004000  UserLogic B  16KB  │  ← 由 Editor 通过串口动态更新
│  (扇区 16-31)                   │     NCC: UserLogic_t 接口表 + ARM 代码
│                                 │     XCODE: XCODE 头 + .wasm 字节码
└─────────────────────────────────┘

SRAM 地址映射：
┌─────────────────────────────────┐
│  0x10000000  Runtime RAM  4KB   │
├─────────────────────────────────┤
│  0x10001000  UserLogic RAM  4KB │
└─────────────────────────────────┘
```

---

## 编译模式

Runtime A 支持两种模式（通过 `make MODE=` 切换），决定如何执行 B 区内容：

### NCC 模式（默认）

B 区为 `arm-none-eabi-gcc` 编译的 ARM 原生机器码。Runtime 读取 B 区起始处的 `UserLogic_t` 接口表，验证魔数后调用 `setup()` / `loop()`。

```
B 区布局（NCC）:
[ UserLogic_t 结构体 | ARM 机器码 ... ]
```

`UserLogic_t` 接口：

```c
typedef struct {
    uint32_t magic;                      // USER_LOGIC_MAGIC (0xDEADBEEF)
    uint32_t version;                    // USER_LOGIC_VERSION
    void    (*setup)(const SystemAPI_t *api);   // 初始化（调用一次）
    void    (*loop)(void);               // 每个扫描周期调用
    uint8_t  di_count;                   // 期望 DI 数量
    uint8_t  do_count;                   // 期望 DO 数量
    uint16_t scan_ms;                    // 扫描周期 ms（0 = 使用 Runtime 默认）
} UserLogic_t;
```

### XCODE 模式

B 区为 WASM 字节码，Runtime A 内嵌 WAMR（WebAssembly Micro Runtime）解释执行，调用 `.wasm` 导出的 `plc_init()` / `plc_run(ms)` 函数。

```
B 区布局（XCODE）:
[ XCODE_WASM_MAGIC 4B | wasm_size 4B | .wasm 字节码 ... ]
```

---

## 目录结构

```
runtime/
├── app/
│   ├── main.c          主循环：验证 B 区 → 调用 NCC 或 XCODE 运行器
│   ├── runtime.c       PLC 扫描调度、I/O 驱动
│   ├── xcode_runner.c  XCODE 模式：WAMR 初始化 + plc_init/plc_run 调用
│   ├── libutil.c       工具函数（字符串、内存等）
│   └── debug.c         调试输出
├── bsp/
│   ├── lpc_chip/       NXP LPC82x 官方 BSP（外设驱动）
│   └── include/        Cortex-M0+ CMSIS 头文件、裸机 libc shim
├── user_logic/         UserLogic B 区示例（NCC 模式，可独立构建）
│   ├── user_logic.c    用户 PLC 逻辑实现
│   ├── lpc824_user.ld  B 区链接脚本（基地址 0x00004000）
│   └── Makefile
├── shared_interface.h  A/B 共享协议（地址常量 + UserLogic_t + SystemAPI_t）
└── makefile            主构建脚本
```

---

## 构建

### 工具链要求

- `arm-none-eabi-gcc`（GNU Arm 嵌入式工具链）
- `arm-none-eabi-objcopy`
- `make`

### 构建 Runtime A

```bash
# NCC 模式（默认）
make

# XCODE 模式（内嵌 WAMR）
make MODE=XCODE

# 查看 ELF 段大小
make dump
```

产物：
- `firmware.elf` — ELF 调试文件
- `firmware.bin` — 烧录到 Flash A（0x00000000, ≤ 16KB）

### 构建 UserLogic B（NCC 示例）

```bash
make user        # 调用 user_logic/Makefile
make user_clean
```

---

## 烧录

使用 `lpc21isp` 通过 ISP 串口烧录 Runtime A：

```bash
make upload TTY=/dev/ttyACM0
# 或
make upload TTY=/dev/ttyUSB0 115200
```

UserLogic B 通过 **TiZi Editor** 的下载功能（串口/TCP）在线更新，无需重新烧录 Runtime A。

---

## SystemAPI — Runtime 提供给 UserLogic 的系统服务

UserLogic（B 区）通过 `SystemAPI_t` 函数表访问硬件服务，不与 Runtime A 的符号链接：

```c
typedef struct {
    uint32_t (*get_tick_ms)(void);           // 系统运行毫秒数
    void     (*uart_puts)(const char *s);    // UART 输出字符串
    void     (*set_do)(uint8_t idx, bool v); // 写数字输出（DO）
    bool     (*get_di)(uint8_t idx);         // 读数字输入（DI）
} SystemAPI_t;
```

---

## WAMR 配置（XCODE 模式）

WAMR 源码位于 `../library/wasm/`，编译时以纯解释模式（Interpreter Only）链接，不使用 AOT / JIT：

| 宏 | 值 |
|----|----|
| `WASM_ENABLE_INTERP` | 1 |
| `WASM_ENABLE_AOT` | 0 |
| `WASM_ENABLE_FAST_JIT` | 0 |
| `WASM_ENABLE_LIBC_WASI` | 0 |
| `WASM_ENABLE_MULTI_MODULE` | 0 |

可通过 `WAMR_ROOT` 变量覆盖 WAMR 路径：

```bash
make MODE=XCODE WAMR_ROOT=/path/to/wamr
```
