#!/usr/bin/env python3
"""
Generate a Mach-O object that defines L_cheng_str_<fnv64> symbols for
all Cheng string literals under a source root.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


def fnv1a64(data: bytes) -> int:
    h = 1469598103934665603
    for b in data:
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def decode_literal(raw: str) -> bytes:
    out = bytearray()
    i = 0
    n = len(raw)
    while i < n:
        ch = raw[i]
        if ch == "\\" and i + 1 < n:
            esc = raw[i + 1]
            if esc == "n":
                out.append(10)
                i += 2
                continue
            if esc == "r":
                out.append(13)
                i += 2
                continue
            if esc == "t":
                out.append(9)
                i += 2
                continue
            if esc == '"':
                out.append(34)
                i += 2
                continue
            if esc == "\\":
                out.append(92)
                i += 2
                continue
            if esc == "0":
                out.append(0)
                i += 2
                continue
            if esc == "x" and i + 3 < n:
                hex_part = raw[i + 2 : i + 4]
                try:
                    out.append(int(hex_part, 16))
                    i += 4
                    continue
                except ValueError:
                    pass
            out.append(ord(esc) & 0xFF)
            i += 2
            continue
        out.append(ord(ch) & 0xFF)
        i += 1
    return bytes(out)


def iter_literals(content: str):
    n = len(content)
    i = 0
    while i < n:
        if content[i] != '"':
            i += 1
            continue
        j = i + 1
        closed = False
        while j < n:
            if content[j] == "\\":
                j += 2
                continue
            if content[j] == '"':
                closed = True
                break
            j += 1
        if not closed:
            break
        raw = content[i + 1 : j]
        yield decode_literal(raw)
        i = j + 1


def collect_label_map(src_root: Path) -> dict[str, bytes]:
    out: dict[str, bytes] = {"L_cheng_str_0000000000000000": b""}
    for path in sorted(src_root.rglob("*.cheng")):
        try:
            content = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for lit in iter_literals(content):
            label = f"L_cheng_str_{fnv1a64(lit):016x}"
            if label not in out:
                out[label] = lit
    return out


def load_labels_file(labels_file: Path) -> list[str]:
    out: list[str] = []
    if not labels_file.exists():
        return out
    pat = re.compile(r"^L_cheng_str_[0-9a-fA-F]{16}$")
    for raw in labels_file.read_text(encoding="utf-8", errors="ignore").splitlines():
        label = raw.strip()
        if label.startswith("_L_cheng_str_"):
            label = label[1:]
        if pat.match(label):
            hex_part = label[len("L_cheng_str_") :]
            out.append("L_cheng_str_" + hex_part.lower())
    return out


def emit_label_bytes(fp, label: str, value: bytes) -> None:
    fp.write(f"{label}:\n")
    fp.write(f"_{label}:\n")
    data = bytearray(value)
    data.append(0)
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        bytes_line = ", ".join(f"0x{b:02x}" for b in chunk)
        fp.write(f"  .byte {bytes_line}\n")
    fp.write("\n")


def write_asm(out_asm: Path, label_map: dict[str, bytes]) -> None:
    out_asm.parent.mkdir(parents=True, exist_ok=True)
    with out_asm.open("w", encoding="utf-8", newline="\n") as fp:
        fp.write(".section __TEXT,__cstring,cstring_literals\n\n")
        for label in sorted(label_map):
            emit_label_bytes(fp, label, label_map[label])


def assemble(out_asm: Path, out_obj: Path, arch: str) -> None:
    out_obj.parent.mkdir(parents=True, exist_ok=True)
    cmd = ["clang", "-c", "-x", "assembler", "-Wa,-L"]
    if arch:
        cmd.extend(["-arch", arch])
    cmd.extend([str(out_asm), "-o", str(out_obj)])
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise SystemExit(proc.returncode)


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo-root", required=True)
    ap.add_argument("--src-root", default="src")
    ap.add_argument("--labels-file", default="")
    ap.add_argument("--labels-only", action="store_true")
    ap.add_argument("--out-asm", required=True)
    ap.add_argument("--out-obj", required=True)
    ap.add_argument("--arch", default="arm64")
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(args.repo_root).resolve()
    src_root = (repo_root / args.src_root).resolve()
    out_asm = Path(args.out_asm).resolve()
    out_obj = Path(args.out_obj).resolve()

    label_map: dict[str, bytes] = {"L_cheng_str_0000000000000000": b""}
    if not args.labels_only:
        if not src_root.exists():
            sys.stderr.write(f"missing src root: {src_root}\n")
            return 2
        label_map = collect_label_map(src_root)
    if args.labels_file:
        labels_file = Path(args.labels_file).resolve()
        for label in load_labels_file(labels_file):
            if label not in label_map:
                label_map[label] = b""
    if not label_map:
        sys.stderr.write(f"no string literals found under: {src_root}\n")
        return 2

    write_asm(out_asm, label_map)
    assemble(out_asm, out_obj, args.arch)
    print(f"gen_cstring_compat_obj: labels={len(label_map)} out={out_obj}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
