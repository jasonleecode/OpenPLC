#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EDITOR_DIR="$ROOT_DIR/editor"
QT_ROOT="${QT_ROOT:-$HOME/Qt/6.5.3/gcc_64}"
MATIEC_DIR="$EDITOR_DIR/tools/matiec_linux"
WORK_DIR="${TMPDIR:-/tmp}/tizi-sim-pipeline-test"
STGEN_BIN="$WORK_DIR/stgen_cli"
PROJECT="${1:-$EDITOR_DIR/tests/fixtures/native_fbd_fb_multi_output.tizi}"
SIM_RUNTIME="$EDITOR_DIR/tools/sim_runtime"

if [[ ! -x "$MATIEC_DIR/iec2c" ]]; then
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

ST_FILE="$WORK_DIR/plc_program.st"
OUT_DIR="$WORK_DIR/iec2c"
SIM_VARS="$WORK_DIR/sim_vars.c"
SIM_BIN="$WORK_DIR/sim_program"

"$STGEN_BIN" "$PROJECT" > "$ST_FILE"
mkdir -p "$OUT_DIR"
"$MATIEC_DIR/iec2c" -p -i -I "$MATIEC_DIR/lib" -T "$OUT_DIR" "$ST_FILE" > "$WORK_DIR/iec2c.out"

python3 - "$OUT_DIR/POUS.h" "$OUT_DIR/POUS.c" "$OUT_DIR/resource1.c" "$SIM_VARS" <<'PY'
import re
import sys
from pathlib import Path

pous = Path(sys.argv[1]).read_text()
pous_code = Path(sys.argv[2]).read_text()
resource = Path(sys.argv[3]).read_text()
out = Path(sys.argv[4])

instance_match = re.search(r"\b([A-Z][A-Z0-9_]*)\s+(RESOURCE1__[A-Z0-9_]+);", resource)
if not instance_match:
    raise SystemExit("cannot find RESOURCE1 program instance")

program_type, instance_name = instance_match.groups()
structs = {}
for struct_match in re.finditer(r"typedef struct \{(?P<body>.*?)\}\s*(?P<type>[A-Z][A-Z0-9_]*);", pous, re.S):
    structs[struct_match.group("type")] = struct_match.group("body")
program_body = structs.get(program_type)
if program_body is None:
    raise SystemExit(f"cannot find struct for {program_type}")

supported = {
    "BOOL": "SIM_VAR_BOOL",
    "INT": "SIM_VAR_INT",
    "DINT": "SIM_VAR_DINT",
    "REAL": "SIM_VAR_REAL",
    "LREAL": "SIM_VAR_LREAL",
}

steps_by_type = {}
for struct_type in structs:
    body_match = re.search(rf"\bvoid\s+{re.escape(struct_type)}_body__\s*\(", pous_code)
    if not body_match:
        continue
    init_starts = [
        match.start()
        for match in re.finditer(rf"\bvoid\s+{re.escape(struct_type)}_init__\s*\(", pous_code[:body_match.start()])
    ]
    if not init_starts:
        continue
    segment = pous_code[init_starts[-1]:body_match.start()]
    steps = [
        (name, int(index))
        for name, index in re.findall(r"#define\s+([A-Z][A-Z0-9_]*)\s+__step_list\[(\d+)\]", segment)
    ]
    if steps:
        steps_by_type[struct_type] = steps

entries = []
seen = set()

def add_entry(display_name, sim_type, address_expr):
    if display_name in seen:
        return
    seen.add(display_name)
    entries.append((display_name, sim_type, f"&{address_expr}"))

def add_struct_vars(struct_type, display_prefix, address_prefix, depth=0):
    if depth > 4:
        return
    body = structs.get(struct_type)
    if not body:
        return

    for iec_type, name in re.findall(r"__DECLARE_VAR\(([^,]+),([^)]+)\)", body):
        iec_type = iec_type.strip()
        name = name.strip()
        if name in {"EN", "ENO"}:
            continue
        sim_type = supported.get(iec_type)
        if sim_type:
            add_entry(f"{display_prefix}.{name}", sim_type, f"{address_prefix}.{name}.value")

    for step_name, step_index in steps_by_type.get(struct_type, []):
        add_entry(
            f"{display_prefix}.{step_name}.X",
            "SIM_VAR_BOOL",
            f"{address_prefix}.__step_list[{step_index}].X.value",
        )

    for child_type, child_name in re.findall(r"^\s*([A-Z][A-Z0-9_]*)\s+([A-Z][A-Z0-9_]*)\s*;", body, re.M):
        if child_type in structs:
            add_struct_vars(
                child_type,
                f"{display_prefix}.{child_name}",
                f"{address_prefix}.{child_name}",
                depth + 1,
            )

add_struct_vars(program_type, "main", instance_name)

if not entries:
    raise SystemExit("no supported variables found")

lines = [
    '#include "sim_api.h"',
    '#include "iec_std_lib.h"',
    '#include "POUS.h"',
    "",
    f"extern {program_type} {instance_name};",
    "",
    "SimVar sim_vars[] = {",
]
for name, sim_type, ptr in entries:
    lines.append(f'    {{"{name}", {sim_type}, {ptr}, 0, 0.0}},')
lines.extend([
    "};",
    "const size_t sim_var_count = sizeof(sim_vars) / sizeof(sim_vars[0]);",
    "",
])
out.write_text("\n".join(lines))
PY

gcc -w \
    -I "$SIM_RUNTIME" \
    -I "$MATIEC_DIR/lib/C" \
    -I "$OUT_DIR" \
    "$SIM_RUNTIME/sim_main.c" \
    "$SIM_VARS" \
    "$OUT_DIR/config.c" \
    "$OUT_DIR/resource1.c" \
    -o "$SIM_BIN" \
    -lm

python3 - "$SIM_BIN" <<'PY'
import json
import subprocess
import sys
import time

proc = subprocess.Popen(
    [sys.argv[1]],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
)

def request(payload):
    assert proc.stdin is not None
    assert proc.stdout is not None
    proc.stdin.write(json.dumps(payload) + "\n")
    proc.stdin.flush()
    line = proc.stdout.readline()
    if not line:
        raise AssertionError("simulator closed stdout")
    reply = json.loads(line)
    if not reply.get("ok", False):
        raise AssertionError(reply)
    return reply

def vars_by_name():
    reply = request({"cmd": "readVars"})
    return {item["name"]: item for item in reply["vars"]}

hello = request({"cmd": "hello"})
assert hello["name"] == "TiZi SmartSim"
assert hello["varCount"] >= 5

request({"cmd": "init"})
state = vars_by_name()

if "main.TRAFIC_LIGHT_SEQUENCE0.GREEN.X" in state:
    request({"cmd": "setTraceVariables", "names": [
        "main.SWITCHBUTTON",
        "main.TRAFIC_LIGHT_SEQUENCE0.ORANGE.X",
        "main.TRAFIC_LIGHT_SEQUENCE0.RED.X",
    ]})
    trace = request({"cmd": "traceData"})
    assert [item["name"] for item in trace["vars"]] == [
        "main.SWITCHBUTTON",
        "main.TRAFIC_LIGHT_SEQUENCE0.ORANGE.X",
        "main.TRAFIC_LIGHT_SEQUENCE0.RED.X",
    ]

    request({"cmd": "forceVar", "name": "main.SWITCHBUTTON", "value": True})
    saw_orange = False
    saw_red = False
    request({"cmd": "start", "intervalMs": 100})
    deadline = time.monotonic() + 4.0
    while time.monotonic() < deadline:
        time.sleep(0.15)
        state = vars_by_name()
        saw_orange = saw_orange or state["main.TRAFIC_LIGHT_SEQUENCE0.ORANGE.X"]["value"]
        saw_red = saw_red or state["main.TRAFIC_LIGHT_SEQUENCE0.RED.X"]["value"]
        if saw_orange and saw_red:
            break
    request({"cmd": "pause"})
    state = vars_by_name()
    assert saw_orange
    assert saw_red
    assert state["main.REDLIGHT"]["value"] is True
    assert state["main.TRAFIC_LIGHT_SEQUENCE0.RED.X"]["value"] is True
    assert state["main.TRAFIC_LIGHT_SEQUENCE0.STOP_CARS"]["value"] is False

    request({"cmd": "releaseForce", "name": "main.SWITCHBUTTON"})
else:
    request({"cmd": "setTraceVariables", "names": ["main.CU", "main.COUNT"]})
    trace = request({"cmd": "traceData"})
    assert [item["name"] for item in trace["vars"]] == ["main.CU", "main.COUNT"]

    request({"cmd": "writeVar", "name": "main.CU", "value": False})
    request({"cmd": "writeVar", "name": "main.R", "value": False})
    request({"cmd": "step"})
    state = vars_by_name()
    assert state["main.COUNT"]["value"] == 0
    assert state["main.DONE"]["value"] is False
    assert state["main.DONEMIRROR"]["value"] is False

    for pulse in range(1, 6):
        request({"cmd": "writeVar", "name": "main.CU", "value": True})
        request({"cmd": "step"})
        state = vars_by_name()
        assert state["main.COUNT"]["value"] == pulse
        assert state["main.DONE"]["value"] == (pulse >= 5)
        assert state["main.DONEMIRROR"]["value"] == (pulse >= 5)

        request({"cmd": "writeVar", "name": "main.CU", "value": False})
        request({"cmd": "step"})

    request({"cmd": "forceVar", "name": "main.CU", "value": True})
    state = vars_by_name()
    assert state["main.CU"]["forced"] is True
    request({"cmd": "releaseForce", "name": "main.CU"})
    state = vars_by_name()
    assert state["main.CU"]["forced"] is False

    request({"cmd": "writeVar", "name": "main.R", "value": True})
    request({"cmd": "step"})
    state = vars_by_name()
    assert state["main.COUNT"]["value"] == 0
    assert state["main.DONE"]["value"] is False

request({"cmd": "stop"})
proc.wait(timeout=2)
assert proc.returncode == 0
PY

echo "Simulation pipeline verification passed."
echo "Artifacts: $WORK_DIR"
