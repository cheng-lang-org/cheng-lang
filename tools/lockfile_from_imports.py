#!/usr/bin/env python3
"""从 Cheng 源文件的 import 指令生成 cheng.lock.toml。

用法: python3 tools/lockfile_from_imports.py <source.cheng> [workspace_root]

遍历源文件的 import 指令，解析每个导入模块的源文件路径，
输出 TOML 格式的锁文件，包含每个模块的路径和 SHA-256 哈希。
"""

import sys
import os
import hashlib

def resolve_import_path(import_path, workspace_root):
    """cheng/core/backend/foo -> src/core/backend/foo.cheng"""
    parts = import_path.split("/")
    # std/xxx -> src/std/xxx.cheng
    # cheng/xxx -> src/xxx.cheng
    if parts[0] == "std":
        path = os.path.join(workspace_root, "src", *parts) + ".cheng"
    elif parts[0] == "cheng":
        path = os.path.join(workspace_root, "src", *parts[1:]) + ".cheng"
    else:
        path = os.path.join(workspace_root, "src", *parts) + ".cheng"
    return path if os.path.isfile(path) else None

def parse_imports(source_path):
    """从源文件中提取 import 路径列表。"""
    imports = []
    with open(source_path, "r") as f:
        for line in f:
            stripped = line.strip()
            if stripped.startswith("import "):
                # import std/os
                # import std/strings as strings
                # import cheng/core/backend/foo as bar
                rest = stripped[7:].strip()
                # 去掉 as 别名
                if " as " in rest:
                    rest = rest.split(" as ")[0].strip()
                imports.append(rest)
    return imports

def sha256_file(path):
    """计算文件的 SHA-256。"""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()

def generate_lockfile(source_path, workspace_root):
    imports = parse_imports(source_path)
    lines = ["# Cheng lock file – auto-generated from import graph"]
    lines.append(f"# Source: {os.path.relpath(source_path, workspace_root)}")
    lines.append("")
    lines.append("[imports]")

    seen = set()
    for imp in imports:
        if imp in seen:
            continue
        seen.add(imp)
        resolved = resolve_import_path(imp, workspace_root)
        if resolved:
            rel = os.path.relpath(resolved, workspace_root)
            sha = sha256_file(resolved)
            lines.append(f'"{imp}" = {{ path = "{rel}", sha256 = "{sha}" }}')
        else:
            lines.append(f'# "{imp}" = {{ path = "NOT FOUND" }}')

    lines.append("")
    return "\n".join(lines) + "\n"

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 tools/lockfile_from_imports.py <source.cheng> [workspace_root]")
        sys.exit(1)

    source = sys.argv[1]
    root = sys.argv[2] if len(sys.argv) > 2 else os.getcwd()

    lockfile = generate_lockfile(source, root)
    print(lockfile)
