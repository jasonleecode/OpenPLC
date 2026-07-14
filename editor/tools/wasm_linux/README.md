# WAMR Linux Runtime

This directory contains Linux x86_64 WAMR runtime artifacts built from:

```text
/home/lixiang/Documents/opensource/wasm-micro-runtime
```

Build used:

```bash
cmake -S product-mini/platforms/linux -B /tmp/wamr-linux-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DWAMR_BUILD_SIMD=0
cmake --build /tmp/wamr-linux-build --parallel 2
cmake --install /tmp/wamr-linux-build --prefix /tmp/wamr-linux-install
```

`WAMR_BUILD_SIMD=0` is intentional for this local build because the default SIMD path tries to fetch `simde` from GitHub, and the current build environment has restricted network access.

Main artifacts:

- `bin/iwasm`
- `lib/libiwasm.a`
- `include/wasm_export.h`
- `include/wasm_c_api.h`
- `include/lib_export.h`
