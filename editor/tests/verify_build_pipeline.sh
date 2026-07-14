#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EDITOR_DIR="$ROOT_DIR/editor"
QT_ROOT="${QT_ROOT:-$HOME/Qt/6.5.3/gcc_64}"
MATIEC_DIR="$EDITOR_DIR/tools/matiec_linux"
WORK_DIR="${TMPDIR:-/tmp}/tizi-build-pipeline-test"
STGEN_BIN="$WORK_DIR/stgen_cli"

if [[ ! -x "$MATIEC_DIR/iec2iec" || ! -x "$MATIEC_DIR/iec2c" ]]; then
    echo "matiec_linux tools are missing or not executable: $MATIEC_DIR" >&2
    exit 1
fi

if [[ ! -d "$QT_ROOT/include/QtCore" || ! -d "$QT_ROOT/include/QtXml" ]]; then
    echo "Qt root not found. Set QT_ROOT to a Qt 6 gcc_64 installation." >&2
    echo "Current QT_ROOT: $QT_ROOT" >&2
    exit 1
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

run_case() {
    local name="$1"
    local project="$2"
    local st_file="$WORK_DIR/$name.st"
    local out_dir="$WORK_DIR/${name}_iec2c"

    "$STGEN_BIN" "$project" > "$st_file"
    "$MATIEC_DIR/iec2iec" -p -i -I "$MATIEC_DIR/lib" "$st_file" > "$WORK_DIR/${name}_iec2iec.out"
    mkdir -p "$out_dir"
    "$MATIEC_DIR/iec2c" -p -i -I "$MATIEC_DIR/lib" -T "$out_dir" "$st_file" > "$WORK_DIR/${name}_iec2c.out"
}

compile_linux_driver() {
    local name="$1"
    gcc -w \
        -I "$MATIEC_DIR/lib/C" \
        -I "$WORK_DIR/${name}_iec2c" \
        "$EDITOR_DIR/tests/drivers/linux/templates/plc_main.c" \
        "$WORK_DIR/${name}_iec2c/config.c" \
        "$WORK_DIR/${name}_iec2c/resource1.c" \
        -o "$WORK_DIR/${name}_program" \
        -lm
}

run_case "plcopen_first_steps" "$EDITOR_DIR/tests/first_steps/plc.tizi"
run_case "plcopen_traffic" "$EDITOR_DIR/tests/first_steps/traffic.tizi"
run_case "native_ld" "$EDITOR_DIR/tests/fixtures/native_ld.tizi"
run_case "native_ld_parallel_reset" "$EDITOR_DIR/tests/fixtures/native_ld_parallel_reset.tizi"
run_case "native_fbd_block_multi_input" "$EDITOR_DIR/tests/fixtures/native_fbd_block_multi_input.tizi"

grep -q "TIZI_TMP" "$WORK_DIR/native_ld.st"
grep -q "TIZI_EDGE3" "$WORK_DIR/native_ld.st"
grep -q "IF TIZI_TMP" "$WORK_DIR/native_ld.st"

grep -q "TIZI_EDGE3" "$WORK_DIR/native_ld_parallel_reset.st"
grep -q " OR " "$WORK_DIR/native_ld_parallel_reset.st"
grep -q "Y := FALSE;" "$WORK_DIR/native_ld_parallel_reset.st"

grep -q "AND(IN1 := (A) OR (B), IN2 := C)" "$WORK_DIR/native_fbd_block_multi_input.st"
grep -q "Y := AND" "$WORK_DIR/native_fbd_block_multi_input.st"
grep -q "SwitchButton : BOOL;" "$WORK_DIR/plcopen_traffic.st"
grep -q "INITIAL_STEP Standstill" "$WORK_DIR/plcopen_traffic.st"

compile_linux_driver "plcopen_traffic"
compile_linux_driver "native_ld"
compile_linux_driver "native_ld_parallel_reset"
compile_linux_driver "native_fbd_block_multi_input"

echo "Build pipeline verification passed."
echo "Artifacts: $WORK_DIR"
