#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MANIFEST="${TOOLCHAIN_MANIFEST:-$ROOT_DIR/editor/tools/toolchain_manifest.json}"

python3 - "$ROOT_DIR" "$MANIFEST" <<'PY'
import json
import os
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
manifest_path = pathlib.Path(sys.argv[2])

with manifest_path.open("r", encoding="utf-8") as fh:
    manifest = json.load(fh)

if manifest.get("schema") != 1:
    raise SystemExit("Unsupported toolchain manifest schema")

tools = manifest.get("tools")
if not isinstance(tools, list) or not tools:
    raise SystemExit("Manifest has no tools")

ids = set()
missing = []
for tool in tools:
    tool_id = tool.get("id")
    if not tool_id:
        raise SystemExit("Tool entry is missing id")
    if tool_id in ids:
        raise SystemExit(f"Duplicate tool id: {tool_id}")
    ids.add(tool_id)

    for key in ("kind", "platform", "version", "paths"):
        if key not in tool:
            raise SystemExit(f"{tool_id} is missing {key}")

    paths = tool["paths"]
    if not isinstance(paths, list) or not paths:
        raise SystemExit(f"{tool_id} has no paths")

    for rel in paths:
        path = root / rel
        if not os.path.lexists(path):
            missing.append(f"{tool_id}: {rel}")

    verify = tool.get("verify")
    if verify and not os.path.isfile(root / verify):
        missing.append(f"{tool_id}: {verify}")

if missing:
    raise SystemExit("Missing manifest paths:\n" + "\n".join(missing))

print(f"Toolchain manifest verification passed: {manifest_path}")
PY
