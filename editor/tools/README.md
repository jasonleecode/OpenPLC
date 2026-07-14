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

`editor/tools/wasm/wasi-sdk` currently contains symlinks from a macOS development machine. On Linux, install a native WASI-SDK and run XCODE verification with:

```bash
WASI_SDK_DIR=/path/to/wasi-sdk editor/tests/verify_xcode_pipeline.sh
```

`editor/tools/wasm/wamrc/` currently contains macOS arm64 `wamrc` binaries and is not usable on Linux.
