# OpenPLC-X

基于 IEC 61131-3 标准的开源 PLC 开发平台，编译器选用 [matiec](https://github.com/beremiz/matiec)，支持 LD、ST、IL、FBD、SFC 等编程语言，兼容 PLCopen XML 格式（可导入 Beremiz 项目）。

<img width="1584" height="1015" alt="TiZi Editor" src="https://github.com/user-attachments/assets/25d98048-7b1e-4d24-80c2-1759bfe5dbc8" />

---

## 项目组成

| 目录 | 说明 |
|------|------|
| `editor/` | TiZi — Qt6 图形化 PLC 编程工具 |
| `runtime/` | PLC Runtime — 运行在目标硬件上的 PLC 执行核心 |
| `library/` | 第三方库（matiec 编译器源码、WAMR WebAssembly 运行时等）|
| `tools/` | 辅助工具（lpc21isp 烧录工具等）|

---

## 编译模式

OpenPLC-X 支持两种编译模式，由 **driver** 的硬件能力决定可选范围，由用户在 Project Settings 中选择：

### NCC — Native Code Compiler（原生机器码）

```
PLC 程序 (LD/ST/...) → matiec → C 代码 → arm-none-eabi-gcc / gcc → 原生可执行文件
```

- 适用于所有目标平台，包括资源受限的 MCU（如 LPC824，8KB RAM）
- 直接运行 ARM/x86 机器码，性能最优
- Runtime B 区存储 `UserLogic_t` 接口表 + ARM 机器码

### XCODE — WebAssembly 字节码

```
PLC 程序 (LD/ST/...) → matiec → C 代码 → wasi-clang → .wasm 字节码
```

- 需要目标平台有足够 RAM 运行 WAMR（WebAssembly Micro Runtime）
- 字节码与平台无关，Runtime A 内嵌 WAMR 解释执行
- Runtime B 区存储 `.wasm` 字节码（头部含魔数 + 大小）
- 适用于资源较丰富的平台（Linux、macOS、资源充裕的 MCU）

---

## 快速开始

### 1. 构建 Editor

```bash
cd editor
cmake -B build -S .
cmake --build build --parallel
# macOS: open build/TiZi.app
# Linux: ./build/TiZi
```

依赖：Qt6（Widgets / Core / Gui / Xml / Svg / SerialPort / Network）、CMake 3.16+

### 2. 构建 Runtime（NCC 模式）

```bash
cd runtime
make                    # 默认 NCC 模式 → firmware.bin
make upload TTY=/dev/ttyACM0   # 通过串口烧录到 LPC824
```

### 3. 构建 Runtime（XCODE 模式）

```bash
cd runtime
make MODE=XCODE         # 内嵌 WAMR → firmware.bin
```

---

## 工作流程

```
┌─────────────────────────────────────────────┐
│              TiZi Editor (PC)               │
│  编写 LD/ST → 编译 → NCC(.bin) / XCODE(.wasm) │
└──────────────────┬──────────────────────────┘
                   │ 通过串口 / TCP 下载到 B 区
┌──────────────────▼──────────────────────────┐
│           PLC Hardware (LPC824)             │
│  Flash A (0x0000, 16KB): Runtime 固件       │
│  Flash B (0x4000, 16KB): UserLogic 程序     │
└─────────────────────────────────────────────┘
```

---

## 相关链接

- [matiec IEC 61131-3 编译器](https://github.com/beremiz/matiec)
- [WAMR WebAssembly Micro Runtime](https://github.com/bytecodealliance/wasm-micro-runtime)
- [PLCopen XML 标准](https://plcopen.org/technical-activities/xml-exchange)
