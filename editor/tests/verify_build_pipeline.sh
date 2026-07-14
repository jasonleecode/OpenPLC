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

"$EDITOR_DIR/tests/verify_matiec_tools.sh" > "$WORK_DIR.matiec.out"
"$EDITOR_DIR/tests/verify_tcp_runtime_server.sh" > "$WORK_DIR.tcp.out"

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

run_edge_scan_test() {
    local name="native_ld_edge_scan"
    local test_c="$WORK_DIR/${name}_test.c"
    local test_bin="$WORK_DIR/${name}_test"

    cat > "$test_c" <<'EDGE_TEST_C'
#include "iec_std_lib.h"
#include "POUS.h"
#include <stdio.h>

TIME __CURRENT_TIME;
BOOL __DEBUG = 0;

extern void config_init__(void);
extern void config_run__(unsigned long tick);
extern MAIN RESOURCE1__MAIN_INSTANCE;

static int check_state(const char *label, int yr, int yf) {
    int ok = 1;
    if (RESOURCE1__MAIN_INSTANCE.YR.value != yr) ok = 0;
    if (RESOURCE1__MAIN_INSTANCE.YF.value != yf) ok = 0;
    if (!ok) {
        fprintf(stderr, "%s: expected YR=%d YF=%d, got YR=%u YF=%u\n",
                label, yr, yf,
                RESOURCE1__MAIN_INSTANCE.YR.value,
                RESOURCE1__MAIN_INSTANCE.YF.value);
        return 1;
    }
    return 0;
}

int main(void) {
    int failures = 0;
    config_init__();

    RESOURCE1__MAIN_INSTANCE.B.value = 0;
    config_run__(0);
    failures += check_state("initial low", 0, 0);

    RESOURCE1__MAIN_INSTANCE.B.value = 1;
    config_run__(1);
    failures += check_state("rising pulse", 1, 0);

    RESOURCE1__MAIN_INSTANCE.B.value = 1;
    config_run__(2);
    failures += check_state("held high", 0, 0);

    RESOURCE1__MAIN_INSTANCE.B.value = 0;
    config_run__(3);
    failures += check_state("falling pulse", 0, 1);

    RESOURCE1__MAIN_INSTANCE.B.value = 0;
    config_run__(4);
    failures += check_state("held low", 0, 0);

    return failures == 0 ? 0 : 1;
}
EDGE_TEST_C

    gcc -w \
        -I "$MATIEC_DIR/lib/C" \
        -I "$WORK_DIR/${name}_iec2c" \
        "$test_c" \
        "$WORK_DIR/${name}_iec2c/config.c" \
        "$WORK_DIR/${name}_iec2c/resource1.c" \
        -o "$test_bin" \
        -lm
    "$test_bin"
}

run_case "plcopen_first_steps" "$EDITOR_DIR/tests/first_steps/plc.tizi"
run_case "plcopen_first_steps_linux" "$EDITOR_DIR/tests/first_steps/plcc.tizi"
run_case "plcopen_traffic" "$EDITOR_DIR/tests/first_steps/traffic.tizi"
run_case "native_ld" "$EDITOR_DIR/tests/fixtures/native_ld.tizi"
run_case "native_ld_edge_scan" "$EDITOR_DIR/tests/fixtures/native_ld_edge_scan.tizi"
run_case "native_ld_parallel_reset" "$EDITOR_DIR/tests/fixtures/native_ld_parallel_reset.tizi"
run_case "native_ld_multi_output_negation" "$EDITOR_DIR/tests/fixtures/native_ld_multi_output_negation.tizi"
run_case "native_fbd_block_multi_input" "$EDITOR_DIR/tests/fixtures/native_fbd_block_multi_input.tizi"
run_case "native_fbd_fb_multi_output" "$EDITOR_DIR/tests/fixtures/native_fbd_fb_multi_output.tizi"

grep -q "TIZI_TMP" "$WORK_DIR/native_ld.st"
grep -q "TIZI_EDGE3" "$WORK_DIR/native_ld.st"
grep -q "IF TIZI_TMP" "$WORK_DIR/native_ld.st"

grep -q "TIZI_EDGE2" "$WORK_DIR/native_ld_edge_scan.st"
grep -q "TIZI_EDGE4" "$WORK_DIR/native_ld_edge_scan.st"

grep -q "TIZI_EDGE3" "$WORK_DIR/native_ld_parallel_reset.st"
grep -q " OR " "$WORK_DIR/native_ld_parallel_reset.st"
grep -q "Y := FALSE;" "$WORK_DIR/native_ld_parallel_reset.st"

grep -q "Y1 := A;" "$WORK_DIR/native_ld_multi_output_negation.st"
grep -q "Y2 := NOT (A);" "$WORK_DIR/native_ld_multi_output_negation.st"
grep -q "B := NOT (A);" "$WORK_DIR/native_ld_multi_output_negation.st"
grep -q "Y3 := NOT (NOT B);" "$WORK_DIR/native_ld_multi_output_negation.st"

grep -q "AND(IN1 := (A) OR (B), IN2 := C)" "$WORK_DIR/native_fbd_block_multi_input.st"
grep -q "Y := AND" "$WORK_DIR/native_fbd_block_multi_input.st"
grep -q "Counter(CU := CU, PV := 5, R := R);" "$WORK_DIR/native_fbd_fb_multi_output.st"
grep -q "Done := Counter.Q;" "$WORK_DIR/native_fbd_fb_multi_output.st"
grep -q "Count := Counter.CV;" "$WORK_DIR/native_fbd_fb_multi_output.st"
grep -q "SwitchButton : BOOL;" "$WORK_DIR/plcopen_traffic.st"
grep -q "INITIAL_STEP Standstill" "$WORK_DIR/plcopen_traffic.st"
grep -q "BLINK_ORANGE_LIGHT(N);" "$WORK_DIR/plcopen_traffic.st"
grep -q "STOP_CARS(D, T#2s);" "$WORK_DIR/plcopen_traffic.st"
grep -q "ACTION BLINK_ORANGE_LIGHT:" "$WORK_DIR/plcopen_traffic.st"
grep -q "TON1(IN := NOT ORANGE_LIGHT, PT := T#500ms);" "$WORK_DIR/plcopen_traffic.st"
grep -q "R_TRIG1(CLK := TON1.Q);" "$WORK_DIR/plcopen_traffic.st"
grep -q ":= NOT (SWITCH_BUTTON);" "$WORK_DIR/plcopen_traffic.st"
awk '
    /TRANSITION FROM PEDESTRIAN_RED TO Standstill/ { in_transition=1 }
    in_transition && /:= NOT SWITCH_BUTTON;/ { ok=1 }
    in_transition && /END_TRANSITION/ { exit ok ? 0 : 1 }
    END { exit ok ? 0 : 1 }
' "$WORK_DIR/plcopen_traffic.st"

compile_linux_driver "plcopen_traffic"
compile_linux_driver "plcopen_first_steps_linux"
compile_linux_driver "native_ld"
compile_linux_driver "native_ld_edge_scan"
compile_linux_driver "native_ld_parallel_reset"
compile_linux_driver "native_ld_multi_output_negation"
compile_linux_driver "native_fbd_block_multi_input"
compile_linux_driver "native_fbd_fb_multi_output"
run_edge_scan_test

echo "Build pipeline verification passed."
echo "Artifacts: $WORK_DIR"
