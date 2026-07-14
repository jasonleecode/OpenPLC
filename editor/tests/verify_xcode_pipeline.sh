#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EDITOR_DIR="$ROOT_DIR/editor"
QT_ROOT="${QT_ROOT:-$HOME/Qt/6.5.3/gcc_64}"
MATIEC_DIR="$EDITOR_DIR/tools/matiec_linux"
WASI_SDK="${WASI_SDK_DIR:-$EDITOR_DIR/tools/wasm/wasi-sdk}"
WORK_DIR="${TMPDIR:-/tmp}/tizi-xcode-pipeline-test"
STGEN_BIN="$WORK_DIR/stgen_cli"
PROJECT="${1:-$EDITOR_DIR/tests/fixtures/native_ld_edge_scan.tizi}"

if [[ ! -x "$MATIEC_DIR/iec2c" ]]; then
    echo "matiec_linux tools are missing or not executable: $MATIEC_DIR" >&2
    exit 1
fi

if [[ ! -d "$QT_ROOT/include/QtCore" || ! -d "$QT_ROOT/include/QtXml" ]]; then
    echo "Qt root not found. Set QT_ROOT to a Qt 6 gcc_64 installation." >&2
    echo "Current QT_ROOT: $QT_ROOT" >&2
    exit 1
fi

if [[ ! -x "$WASI_SDK/bin/clang" ]]; then
    echo "Skipping XCODE verification: WASI-SDK clang not found or not executable." >&2
    echo "Set WASI_SDK_DIR to a Linux WASI-SDK installation to run this test." >&2
    echo "Current WASI_SDK_DIR: $WASI_SDK" >&2
    if [[ "${REQUIRE_XCODE_TOOLS:-0}" == "1" ]]; then
        exit 1
    fi
    exit 0
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

c++ -std=gnu++17 -fPIC -pthread \
    -I"$EDITOR_DIR" \
    -isystem "$QT_ROOT/include" \
    -isystem "$QT_ROOT/include/QtCore" \
    -isystem "$QT_ROOT/include/QtXml" \
    -isystem "$QT_ROOT/mkspecs/linux-g++" \
    "$EDITOR_DIR/tests/stgen_cli.cpp" \
    "$EDITOR_DIR/src/core/compiler/StGenerator.cpp" \
    -Wl,-rpath,"$QT_ROOT/lib" \
    "$QT_ROOT/lib/libQt6Core.so.6.5.3" \
    "$QT_ROOT/lib/libQt6Xml.so.6.5.3" \
    -o "$STGEN_BIN"

ST_FILE="$WORK_DIR/plc_program.st"
OUT_DIR="$WORK_DIR/iec2c"
WRAPPER="$WORK_DIR/plc_program_main.c"
WASM="$WORK_DIR/plc_program.wasm"
IMAGE="$WORK_DIR/plc_program.xcode.bin"

"$STGEN_BIN" "$PROJECT" > "$ST_FILE"
mkdir -p "$OUT_DIR"
"$MATIEC_DIR/iec2c" -p -i -I "$MATIEC_DIR/lib" -T "$OUT_DIR" "$ST_FILE" > "$WORK_DIR/iec2c.out"
cp "$EDITOR_DIR/tests/drivers/linux/templates/plc_wasm_main.c" "$WRAPPER"

"$WASI_SDK/bin/clang" \
    --sysroot="$WASI_SDK/share/wasi-sysroot" \
    --target=wasm32-wasi \
    -Os \
    -ffunction-sections \
    -fdata-sections \
    -w \
    -I "$MATIEC_DIR/lib/C" \
    -I "$OUT_DIR" \
    "$WRAPPER" \
    "$OUT_DIR/config.c" \
    "$OUT_DIR/resource1.c" \
    -o "$WASM" \
    -Wl,--no-entry \
    -Wl,--export=plc_init \
    -Wl,--export=plc_run \
    -Wl,--allow-undefined

python3 - "$WASM" "$IMAGE" <<'PY'
import pathlib
import struct
import sys

wasm = pathlib.Path(sys.argv[1]).read_bytes()
pathlib.Path(sys.argv[2]).write_bytes(struct.pack("<II", 0x57415300, len(wasm)) + wasm)
PY

python3 - "$WASM" "$IMAGE" <<'PY'
import pathlib
import struct
import sys

wasm = pathlib.Path(sys.argv[1]).read_bytes()
image = pathlib.Path(sys.argv[2]).read_bytes()
magic, size = struct.unpack("<II", image[:8])
assert magic == 0x57415300, hex(magic)
assert size == len(wasm), (size, len(wasm))
assert image[8:] == wasm
PY

if [[ -x "$WASI_SDK/bin/llvm-objdump" ]]; then
    "$WASI_SDK/bin/llvm-objdump" -x "$WASM" > "$WORK_DIR/wasm-objdump.txt"
    grep -q "plc_init" "$WORK_DIR/wasm-objdump.txt"
    grep -q "plc_run" "$WORK_DIR/wasm-objdump.txt"
fi

echo "XCODE pipeline verification passed."
echo "Artifacts: $WORK_DIR"
