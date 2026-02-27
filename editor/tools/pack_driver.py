#!/usr/bin/env python3
"""
pack_driver.py — TiZi Driver Packager
======================================
将单个 driver 目录打包成 TiZi 专用的 .cab 文件，用于 driver 的分发和安装。

用法:
    python pack_driver.py <driver_dir>              # 打包到当前目录下 <dir_name>.cab
    python pack_driver.py <driver_dir> -o out.cab   # 指定输出路径
    python pack_driver.py --inspect <file.cab>       # 查看 .cab 内容（不安装）

.cab 文件格式（二进制，小端序）:
    [4B]  Magic: b'TZDR'
    [1B]  Version: 1
    [4B]  Manifest JSON 长度 (uint32 LE)
    [N]   Manifest JSON (UTF-8)  ← 含 dir_name / name / description / mode 等
    [4B]  文件数量 (uint32 LE)
    对每个文件:
      [2B]  相对路径长度 (uint16 LE, UTF-8)
      [N]   相对路径（以 '/' 分隔，不含前导斜杠）
      [4B]  文件数据长度 (uint32 LE)
      [N]   文件原始字节
"""

import sys
import os
import json
import struct
import argparse
import datetime
from pathlib import Path

MAGIC   = b'TZDR'
VERSION = 1


# ── 打包 ──────────────────────────────────────────────────────────────────────

def pack_driver(driver_dir: Path, output_path: Path) -> None:
    """将 driver_dir 目录打包成 output_path 指定的 .cab 文件。"""
    driver_dir = driver_dir.resolve()

    # 校验 driver.json 存在
    driver_json_path = driver_dir / "driver.json"
    if not driver_json_path.exists():
        raise FileNotFoundError(f"driver.json not found in {driver_dir}")

    with open(driver_json_path, encoding="utf-8") as f:
        driver_info = json.load(f)

    # 构建 manifest（供安装端读取元数据）
    manifest = {
        "dir_name":    driver_dir.name,
        "name":        driver_info.get("name", driver_dir.name),
        "description": driver_info.get("description", ""),
        "mode":        driver_info.get("mode", "NCC"),
        "pack_time":   datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    manifest_bytes = json.dumps(manifest, ensure_ascii=False, indent=None).encode("utf-8")

    # 收集所有文件（递归），跳过隐藏文件/目录
    files: list[tuple[str, bytes]] = []
    for root, dirs, filenames in os.walk(driver_dir):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for filename in sorted(filenames):
            if filename.startswith("."):
                continue
            file_path = Path(root) / filename
            rel = file_path.relative_to(driver_dir)
            rel_str = "/".join(rel.parts)   # 统一使用 '/' 分隔符
            files.append((rel_str, file_path.read_bytes()))

    # 写入 .cab
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "wb") as out:
        # 文件头
        out.write(MAGIC)
        out.write(struct.pack("B", VERSION))
        out.write(struct.pack("<I", len(manifest_bytes)))
        out.write(manifest_bytes)
        out.write(struct.pack("<I", len(files)))

        # 文件条目
        for rel_path, data in files:
            path_bytes = rel_path.encode("utf-8")
            out.write(struct.pack("<H", len(path_bytes)))
            out.write(path_bytes)
            out.write(struct.pack("<I", len(data)))
            out.write(data)

    total_bytes = sum(len(d) for _, d in files)
    print(f"Driver : {manifest['name']}")
    print(f"Mode   : {manifest['mode']}")
    print(f"Files  : {len(files)}")
    for rel_path, data in files:
        print(f"         {rel_path:<45s}  {len(data):>6} B")
    print(f"Output : {output_path}  ({output_path.stat().st_size} B total, {total_bytes} B payload)")


# ── 查看 ──────────────────────────────────────────────────────────────────────

def inspect_cab(cab_path: Path) -> None:
    """打印 .cab 文件内容摘要（不解包）。"""
    with open(cab_path, "rb") as f:
        magic = f.read(4)
        if magic != MAGIC:
            raise ValueError(f"Not a TiZi driver package (bad magic: {magic!r})")

        version, = struct.unpack("B", f.read(1))
        if version != VERSION:
            raise ValueError(f"Unsupported version: {version}")

        manifest_len, = struct.unpack("<I", f.read(4))
        manifest = json.loads(f.read(manifest_len).decode("utf-8"))

        file_count, = struct.unpack("<I", f.read(4))

        print(f"TiZi Driver Package — {cab_path.name}")
        print(f"  Name       : {manifest.get('name', '?')}")
        print(f"  dir_name   : {manifest.get('dir_name', '?')}")
        print(f"  Mode       : {manifest.get('mode', '?')}")
        print(f"  Description: {manifest.get('description', '')}")
        print(f"  Packed     : {manifest.get('pack_time', '?')}")
        print(f"  Files ({file_count}):")

        for _ in range(file_count):
            path_len, = struct.unpack("<H", f.read(2))
            rel_path  = f.read(path_len).decode("utf-8")
            data_len, = struct.unpack("<I", f.read(4))
            f.seek(data_len, 1)   # 跳过数据
            print(f"    {rel_path:<50s}  {data_len:>6} B")


# ── 入口 ──────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="TiZi Driver Packager — pack/inspect .cab driver packages",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Pack a driver:
    python pack_driver.py ../tests/drivers/linux
    python pack_driver.py ../tests/drivers/lpc824 -o lpc824_v2.cab

  Inspect a package without installing:
    python pack_driver.py --inspect linux.cab
""",
    )

    parser.add_argument(
        "target",
        help="Driver directory to pack, OR .cab file path when --inspect is used",
    )
    parser.add_argument(
        "-o", "--output",
        metavar="FILE",
        help="Output .cab path (default: <driver_dir_name>.cab in current directory)",
    )
    parser.add_argument(
        "--inspect",
        action="store_true",
        help="Inspect an existing .cab file instead of packing",
    )

    args = parser.parse_args()
    target = Path(args.target)

    if args.inspect:
        if not target.exists():
            parser.error(f"File not found: {target}")
        inspect_cab(target)
    else:
        if not target.is_dir():
            parser.error(f"Not a directory: {target}")
        output = Path(args.output) if args.output else Path(f"{target.name}.cab")
        pack_driver(target, output)


if __name__ == "__main__":
    main()
