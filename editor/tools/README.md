# Editor Toolchain

This directory stores tool binaries used by the TiZi editor build pipeline.

## matiec

Required layout:

```text
editor/tools/matiec_linux/
  iec2iec
  iec2c
  lib/

editor/tools/matiec_mac/
  iec2iec
  iec2c
  lib/
```

Current checked-in binaries:

- `matiec_linux`: Linux x86_64 ELF binaries.
- `matiec_mac`: macOS arm64 Mach-O binaries.

Verify the active toolchain with:

```bash
editor/tests/verify_matiec_tools.sh
```

Run the full editor build pipeline regression with:

```bash
editor/tests/verify_build_pipeline.sh
```

## WASI / WAMR

`editor/tools/wasm/wasi-sdk` is the default in-tree WASI-SDK location. The
checked-in contents are macOS-oriented symlinks, so Linux developers should
install a native WASI-SDK and pass it explicitly:

```bash
WASI_SDK_DIR=/path/to/wasi-sdk editor/tests/verify_xcode_pipeline.sh
```

If a different native WAMR runtime should be used for the runtime execution check:

```bash
WAMR_IWASM=/path/to/iwasm WASI_SDK_DIR=/path/to/wasi-sdk editor/tests/verify_xcode_pipeline.sh
```

`editor/tools/wasm/wamrc/` contains macOS arm64 `wamrc` binaries. Keep this
directory for macOS usage; Linux should use native WAMR tools instead.

`editor/tools/wasm_linux/` contains Linux x86_64 WAMR `iwasm` runtime artifacts built from `wasm-micro-runtime` 2.4.3:

```bash
editor/tests/verify_wamr_linux.sh
```
