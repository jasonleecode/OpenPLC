#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EDITOR_DIR="$ROOT_DIR/editor"
MATIEC_DIR="${MATIEC_DIR:-$EDITOR_DIR/tools/matiec_linux}"
WORK_DIR="${TMPDIR:-/tmp}/tizi-matiec-tools-test"

for tool in iec2iec iec2c; do
    if [[ ! -x "$MATIEC_DIR/$tool" ]]; then
        echo "Missing or non-executable matiec tool: $MATIEC_DIR/$tool" >&2
        exit 1
    fi
done

case "$(uname -s)" in
    Linux)
        if command -v file >/dev/null 2>&1; then
            file "$MATIEC_DIR/iec2iec" "$MATIEC_DIR/iec2c" | grep -q "ELF 64-bit"
        fi
        ;;
    Darwin)
        if command -v file >/dev/null 2>&1; then
            file "$MATIEC_DIR/iec2iec" "$MATIEC_DIR/iec2c" | grep -q "Mach-O"
        fi
        ;;
esac

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/iec2c"

cat > "$WORK_DIR/smoke.st" <<'ST'
PROGRAM main
VAR
  A : BOOL;
END_VAR
  A := TRUE;
END_PROGRAM

CONFIGURATION config
  RESOURCE resource1 ON PLC
    TASK main_task(INTERVAL := T#10ms, PRIORITY := 0);
    PROGRAM main_instance WITH main_task : main;
  END_RESOURCE
END_CONFIGURATION
ST

"$MATIEC_DIR/iec2iec" -p -i -I "$MATIEC_DIR/lib" "$WORK_DIR/smoke.st" > "$WORK_DIR/iec2iec.out"
"$MATIEC_DIR/iec2c" -p -i -I "$MATIEC_DIR/lib" -T "$WORK_DIR/iec2c" "$WORK_DIR/smoke.st" > "$WORK_DIR/iec2c.out"

test -s "$WORK_DIR/iec2c/config.c"
test -s "$WORK_DIR/iec2c/resource1.c"
test -s "$WORK_DIR/iec2c/POUS.c"

echo "matiec tool verification passed: $MATIEC_DIR"
