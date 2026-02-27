# TiZi（梯子）— PLC 图形化编程工具

基于 Qt6 的 IEC 61131-3 PLC IDE，仿 Beremiz 风格，支持 LD、ST、IL、FBD、SFC 编程语言，兼容 PLCopen XML 格式。

---

## 功能特性

- **多语言编辑器**：梯形图（LD）图形编辑、ST/IL 文本编辑（含语法高亮）、FBD/SFC 图形查看
- **PLCopen XML 兼容**：可直接打开 Beremiz 导出的 `.xml` 项目文件
- **完整编译流水线**：PLCopen → ST → matiec iec2c → C 代码 → NCC/XCODE 目标产物
- **双模式编译**：NCC（原生机器码）/ XCODE（WebAssembly 字节码）
- **Driver 架构**：通过 `driver.json` 描述目标硬件，一个文件配置编译器、链接脚本、模板
- **下载支持**：通过串口 / TCP 将编译产物下载到 PLC 设备的 B 区
- **Undo/Redo**：图形编辑器支持完整的撤销/重做历史
- **MVC 架构**：`ProjectModel` / `PouModel` 数据层 + Qt Widgets 视图层

---

## 目录结构

```
editor/
├── src/
│   ├── app/            主窗口 (MainWindow)
│   ├── core/
│   │   ├── models/     数据模型 (ProjectModel, PouModel, VariableDecl)
│   │   └── compiler/   代码生成 (StGenerator, CodeGenerator)
│   ├── editor/
│   │   ├── items/      图形元件 (ContactItem, CoilItem, FunctionBlockItem, VarBoxItem, WireItem)
│   │   └── scene/      画布 (PlcOpenViewer, LadderScene, LadderView)
│   ├── comm/           通信层 (串口/TCP 传输, 下载协议)
│   ├── utils/          工具 (StHighlighter, UndoStack)
│   └── conf/           配置文件 (library.xml — IEC 标准函数库定义)
├── resources/          QSS 主题, 图标资源
├── tests/
│   ├── drivers/        目标平台 Driver 配置
│   │   ├── linux/      Linux x86_64 (NCC + XCODE)
│   │   ├── macos/      macOS (NCC + XCODE)
│   │   └── lpc824/     NXP LPC824 Cortex-M0+ (仅 NCC)
│   └── first_steps/    示例项目 (Beremiz First Steps demo)
└── tools/
    ├── matiec_mac/     matiec 工具 (iec2iec, iec2c, lib/)
    ├── matiec_linux/   matiec 工具 Linux 版本
    └── wasm/           WASI-SDK (用户自行准备, 放于 wasm/wasi-sdk/)
```

---

## 构建

```bash
# 在 editor/ 目录下
cmake -B build -S .
cmake --build build --parallel

# macOS
open build/TiZi.app

# Linux
./build/TiZi
```

**依赖**：Qt6（Widgets / Core / Gui / Xml / Svg / SerialPort / Network / PrintSupport）、CMake 3.16+

---

## 编译流水线

项目编译分 5 步（Build 按钮触发）：

```
1. StGenerator::fromXml()   PLCopen XML → IEC 61131-3 ST 文本
2. iec2iec -p -i            ST 语法校验
3. iec2c   -p -i -T <out>   ST → C 代码 (POUS.c / config.c / resource*.c)
4. 读取 driver 模板          生成 wrapper 入口文件 (plc_main.c / plc_wasm_main.c)
5a. [NCC]  gcc / arm-none-eabi-gcc → 可执行文件 / .elf → 可选 objcopy → .bin
5b. [XCODE] wasi-clang → .wasm 字节码
```

构建产物输出到：`<app>/output/<项目名>/`

---

## Driver 系统

每个目标平台对应 `tests/drivers/<name>/` 目录，包含：

```
driver.json          硬件描述 + 编译配置
templates/           wrapper 模板 C 文件
linker/              链接脚本 (嵌入式平台)
include/             平台专用头文件 (嵌入式平台)
```

**driver.json 结构**：

```json
{
  "name": "Linux x86_64",
  "mode": ["NCC", "XCODE"],        // 硬件支持的模式（字符串=单一，数组=多选）
  "compiler": {
    "ncc": {
      "cc": "gcc",
      "cflags": ["-w"],
      "ldflags": ["-lm"],
      "template": "templates/plc_main.c",
      "output_suffix": ""
    },
    "xcode": {
      "cflags": ["--target=wasm32-wasi", "-Os"],
      "ldflags": ["-Wl,--no-entry", "-Wl,--export=plc_init", "-Wl,--export=plc_run"],
      "template": "templates/plc_wasm_main.c",
      "output_suffix": ".wasm"
    }
  }
}
```

`mode` 字段决定硬件能力约束：若为单一字符串（如 `"NCC"`），Project Settings 中 Mode 下拉框自动禁用，提示用户该硬件的限制。

---

## XCODE 模式 — WASI-SDK 准备

XCODE 模式需要 WASI-SDK（WebAssembly System Interface 交叉编译工具链）：

1. 从 [WASI-SDK Releases](https://github.com/WebAssembly/wasi-sdk/releases) 下载对应平台的包
2. 解压后放到 `editor/tools/wasm/wasi-sdk/`

目录结构应为：
```
editor/tools/wasm/wasi-sdk/
├── bin/
│   └── clang        ← wasi-clang 编译器
└── share/
    └── wasi-sysroot/
```

---

## 文件格式

| 扩展名 | 说明 |
|--------|------|
| `.tizi` | TiZi 原生格式（XML），也兼容 PLCopen XML |
| `.xml` | PLCopen XML（Beremiz 兼容） |

两种格式均可通过 File → Open 直接打开。
