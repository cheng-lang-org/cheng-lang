#!/usr/bin/env python3
"""
Generate a Mach-O object that defines L_cheng_str_<fnv64> symbols for
all Cheng string literals under a source root, or patch Mach-O object
__TEXT,__cstring payloads for bootstrap compatibility.
"""

from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path


LABEL_RE = re.compile(r"^_?L_cheng_str_([0-9a-fA-F]{16})$")
LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2


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


def iter_cheng_files(path: Path):
    if path.is_file() and path.suffix == ".cheng":
        yield path
        return
    if path.is_dir():
        yield from sorted(path.rglob("*.cheng"))


def collect_label_map(
    src_root: Path | None,
    extra_paths: list[Path],
    needed_labels: set[str] | None = None,
) -> dict[str, bytes]:
    out: dict[str, bytes] = {"L_cheng_str_0000000000000000": b""}
    seen: set[Path] = set()
    scan_roots: list[Path] = []
    scan_roots.extend(extra_paths)
    if src_root is not None and src_root.exists():
        scan_roots.append(src_root)
    remaining = None if needed_labels is None else set(needed_labels) - set(out)
    for root in scan_roots:
        for path in iter_cheng_files(root):
            try:
                resolved = path.resolve()
            except OSError:
                resolved = path
            if resolved in seen:
                continue
            seen.add(resolved)
            try:
                content = path.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            for lit in iter_literals(content):
                label = f"L_cheng_str_{fnv1a64(lit):016x}"
                if label in out:
                    continue
                if remaining is not None and label not in remaining:
                    continue
                out[label] = lit
                if remaining is not None:
                    remaining.discard(label)
                    if not remaining:
                        return out
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


def parse_cstring(buf: bytes) -> str:
    end = buf.find(b"\x00")
    if end < 0:
        end = len(buf)
    return buf[:end].decode("utf-8", errors="ignore")


def parse_macho64_layout(blob: bytes) -> tuple[tuple[int, int, int], tuple[int, int, int, int]]:
    if len(blob) < 32:
        raise SystemExit("truncated Mach-O header")
    magic = struct.unpack_from("<I", blob, 0)[0]
    if magic != 0xFEEDFACF:
        raise SystemExit(f"unsupported Mach-O magic: 0x{magic:08x}")
    ncmds = struct.unpack_from("<I", blob, 16)[0]
    cmd_off = 32
    cstring = None
    symtab = None
    for _ in range(ncmds):
        if cmd_off + 8 > len(blob):
            raise SystemExit("truncated load commands")
        cmd, cmdsize = struct.unpack_from("<II", blob, cmd_off)
        if cmdsize < 8 or cmd_off + cmdsize > len(blob):
            raise SystemExit("invalid load command size")
        if cmd == LC_SEGMENT_64:
            nsects = struct.unpack_from("<I", blob, cmd_off + 64)[0]
            sect_off = cmd_off + 72
            for _ in range(nsects):
                if sect_off + 80 > cmd_off + cmdsize:
                    raise SystemExit("truncated section table")
                sectname = parse_cstring(blob[sect_off : sect_off + 16])
                segname = parse_cstring(blob[sect_off + 16 : sect_off + 32])
                addr, size = struct.unpack_from("<QQ", blob, sect_off + 32)
                offset = struct.unpack_from("<I", blob, sect_off + 48)[0]
                if sectname == "__cstring" and segname == "__TEXT":
                    cstring = (addr, size, offset)
                sect_off += 80
        elif cmd == LC_SYMTAB:
            symoff, nsyms, stroff, strsize = struct.unpack_from("<IIII", blob, cmd_off + 8)
            symtab = (symoff, nsyms, stroff, strsize)
        cmd_off += cmdsize
    if cstring is None:
        raise SystemExit("missing __TEXT,__cstring section")
    if symtab is None:
        raise SystemExit("missing LC_SYMTAB")
    return cstring, symtab


def parse_label_symbols(blob: bytes, section_addr: int, section_size: int, symtab: tuple[int, int, int, int]):
    symoff, nsyms, stroff, strsize = symtab
    out: list[tuple[str, int, int]] = []
    if symoff + (nsyms * 16) > len(blob) or stroff + strsize > len(blob):
        raise SystemExit("truncated symbol or string table")
    strtab = blob[stroff : stroff + strsize]
    for idx in range(nsyms):
        entry_off = symoff + (idx * 16)
        n_strx = struct.unpack_from("<I", blob, entry_off)[0]
        n_value = struct.unpack_from("<Q", blob, entry_off + 8)[0]
        if n_strx >= len(strtab):
            continue
        raw_name = parse_cstring(strtab[n_strx:])
        match = LABEL_RE.match(raw_name)
        if not match:
            continue
        if n_value < section_addr or n_value > section_addr + section_size:
            continue
        out.append((f"L_cheng_str_{match.group(1).lower()}", n_value, entry_off))
    out.sort(key=lambda item: (item[1], item[0], item[2]))
    return out


def patch_symbol_value(blob: bytearray, entry_off: int, new_value: int) -> None:
    struct.pack_into("<Q", blob, entry_off + 8, new_value)


def patch_object(obj_path: Path, label_map: dict[str, bytes]) -> tuple[int, int]:
    blob = bytearray(obj_path.read_bytes())
    try:
        (section_addr, section_size, section_offset), symtab = parse_macho64_layout(blob)
    except SystemExit as exc:
        if str(exc) == "missing __TEXT,__cstring section":
            return 0, 0
        raise
    labels = parse_label_symbols(blob, section_addr, section_size, symtab)
    if not labels:
        return 0, 0
    patched = 0
    skipped = 0
    section_end = section_offset + section_size
    unique_labels: dict[str, tuple[int, list[int]]] = {}
    for label, addr, entry_off in labels:
        if label not in unique_labels:
            unique_labels[label] = (addr, [entry_off])
        else:
            prev_addr, entry_list = unique_labels[label]
            if addr < prev_addr:
                unique_labels[label] = (addr, entry_list + [entry_off])
            else:
                entry_list.append(entry_off)
    if len(unique_labels) == 2:
        empty_item = unique_labels.get("L_cheng_str_14650fb0739d0383")
        nonempty_items = [(label, item) for label, item in unique_labels.items() if label != "L_cheng_str_14650fb0739d0383"]
        if empty_item is not None and len(nonempty_items) == 1:
            nonempty_label, (nonempty_addr, nonempty_entries) = nonempty_items[0]
            empty_addr, empty_entries = empty_item
            nonempty_value = label_map.get(nonempty_label)
            if (
                nonempty_value is not None
                and len(nonempty_value) > 0
                and section_size >= len(nonempty_value) + 2
                and nonempty_addr == section_addr
                and empty_addr == section_addr + len(nonempty_value)
            ):
                layout = b"\x00" + nonempty_value + b"\x00"
                blob[section_offset : section_offset + len(layout)] = layout
                for entry_off in empty_entries:
                    patch_symbol_value(blob, entry_off, section_addr)
                for entry_off in nonempty_entries:
                    patch_symbol_value(blob, entry_off, section_addr + 1)
                obj_path.write_bytes(blob)
                return 2, 0
    for idx, (label, addr, _entry_off) in enumerate(labels):
        value = label_map.get(label)
        if value is None:
            skipped += 1
            continue
        file_off = section_offset + (addr - section_addr)
        next_file_off = section_end
        j = idx + 1
        while j < len(labels):
            if labels[j][1] != addr:
                next_file_off = section_offset + (labels[j][1] - section_addr)
                break
            j += 1
        payload = value + b"\x00"
        payload_no_nul = value
        if file_off < section_offset or file_off >= section_end:
            skipped += 1
            continue
        if file_off + len(payload) <= next_file_off:
            if blob[file_off : file_off + len(payload)] != payload:
                blob[file_off : file_off + len(payload)] = payload
                patched += 1
            continue
        if file_off + len(payload_no_nul) <= next_file_off:
            if blob[file_off : file_off + len(payload_no_nul)] != payload_no_nul:
                blob[file_off : file_off + len(payload_no_nul)] = payload_no_nul
                patched += 1
            continue
        skipped += 1
    if patched > 0:
        obj_path.write_bytes(blob)
    return patched, skipped


def required_patch_labels(obj_path: Path) -> set[str]:
    blob = obj_path.read_bytes()
    try:
        (section_addr, section_size, _section_offset), symtab = parse_macho64_layout(blob)
    except SystemExit as exc:
        if str(exc) == "missing __TEXT,__cstring section":
            return set()
        raise
    labels = parse_label_symbols(blob, section_addr, section_size, symtab)
    return {label for label, _addr, _entry_off in labels}


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo-root", required=True)
    ap.add_argument("--src-root", default="src")
    ap.add_argument("--labels-file", default="")
    ap.add_argument("--labels-only", action="store_true")
    ap.add_argument("--out-asm", default="")
    ap.add_argument("--out-obj", default="")
    ap.add_argument("--arch", default="arm64")
    ap.add_argument("--patch-obj", default="")
    ap.add_argument("--extra-source", action="append", default=[])
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(args.repo_root).resolve()
    src_root = (repo_root / args.src_root).resolve()
    extra_paths = [Path(raw).resolve() for raw in args.extra_source if raw]
    patch_obj_path = Path(args.patch_obj).resolve() if args.patch_obj else None
    needed_labels: set[str] | None = None

    label_map: dict[str, bytes] = {"L_cheng_str_0000000000000000": b""}
    if patch_obj_path is not None:
        if not patch_obj_path.exists():
            sys.stderr.write(f"missing object: {patch_obj_path}\n")
            return 2
        needed_labels = required_patch_labels(patch_obj_path)
        if not needed_labels:
            patched, skipped = patch_object(patch_obj_path, label_map)
            print(f"patch_macho_cstrings: patched={patched} skipped={skipped} obj={patch_obj_path}")
            return 0
    if not args.labels_only:
        if not src_root.exists():
            sys.stderr.write(f"missing src root: {src_root}\n")
            return 2
        label_map = collect_label_map(src_root, extra_paths, needed_labels)
    elif extra_paths:
        label_map.update(collect_label_map(None, extra_paths, needed_labels))
    if args.labels_file:
        labels_file = Path(args.labels_file).resolve()
        for label in load_labels_file(labels_file):
            if label not in label_map:
                label_map[label] = b""

    if args.patch_obj:
        assert patch_obj_path is not None
        patched, skipped = patch_object(patch_obj_path, label_map)
        print(f"patch_macho_cstrings: patched={patched} skipped={skipped} obj={patch_obj_path}")
        return 0

    out_asm = Path(args.out_asm).resolve()
    out_obj = Path(args.out_obj).resolve()
    if not label_map:
        sys.stderr.write(f"no string literals found under: {src_root}\n")
        return 2
    if not args.out_asm or not args.out_obj:
        sys.stderr.write("missing --out-asm/--out-obj for generate mode\n")
        return 2
    write_asm(out_asm, label_map)
    assemble(out_asm, out_obj, args.arch)
    print(f"gen_cstring_compat_obj: labels={len(label_map)} out={out_obj}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
