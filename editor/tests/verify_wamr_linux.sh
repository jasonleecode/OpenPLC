#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WAMR_DIR="${WAMR_LINUX_DIR:-$ROOT_DIR/editor/tools/wasm_linux}"
IWASM="$WAMR_DIR/bin/iwasm"
LIBIWASM="$WAMR_DIR/lib/libiwasm.a"
INCLUDE_DIR="$WAMR_DIR/include"
WORK_DIR="$(mktemp -d -t tizi-wamr-linux.XXXXXX)"

cleanup() {
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

require_file() {
  local path="$1"
  if [[ ! -f "$path" ]]; then
    echo "Missing required WAMR artifact: $path" >&2
    exit 1
  fi
}

if [[ ! -x "$IWASM" ]]; then
  echo "Missing executable iwasm: $IWASM" >&2
  exit 1
fi

require_file "$LIBIWASM"
require_file "$INCLUDE_DIR/wasm_export.h"
require_file "$INCLUDE_DIR/wasm_c_api.h"
require_file "$INCLUDE_DIR/lib_export.h"

if ! file -L "$IWASM" | grep -Eq 'ELF .*x86-64|ELF .*x86_64'; then
  file -L "$IWASM" >&2
  echo "iwasm is not a Linux x86_64 ELF executable" >&2
  exit 1
fi

if ! file "$LIBIWASM" | grep -q 'current ar archive'; then
  file "$LIBIWASM" >&2
  echo "libiwasm.a is not a static archive" >&2
  exit 1
fi

version_output="$("$IWASM" --version)"
if ! grep -q 'iwasm 2\.4\.3' <<<"$version_output"; then
  echo "$version_output" >&2
  echo "Unexpected iwasm version" >&2
  exit 1
fi

cat > "$WORK_DIR/check_wamr.c" <<'C'
#include "wasm_export.h"
#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
    wasm_runtime_get_version(&major, &minor, &patch);
    printf("%u.%u.%u\n", major, minor, patch);
    return major == 2 && minor == 4 && patch == 3 ? 0 : 1;
}
C

gcc \
  -I "$INCLUDE_DIR" \
  "$WORK_DIR/check_wamr.c" \
  "$LIBIWASM" \
  -lm -ldl -lpthread \
  -o "$WORK_DIR/check_wamr"

"$WORK_DIR/check_wamr" > "$WORK_DIR/check_wamr.out"
grep -q '^2\.4\.3$' "$WORK_DIR/check_wamr.out"

echo "WAMR Linux toolchain verification passed."
