# CSG v2 Architecture

Cheng 编译器的冷启动后端架构。public facts 使用 canonical `CHENG_CSG_V2`；internal `CHENGCSG` 只用于 cold 自检快照。

## Pipeline

```
Cheng Source
  └─ emit-cold-csg-v2
       └─ parser → BodyIR/codegen-ready function set
            └─ canonical CSG v2 facts (CHENG_CSG_V2)

Internal self-test only
  └─ system-link-exec --emit:csg-v2
       └─ BodyIR snapshot facts (CHENGCSG)

CSG v2 facts
  └─ C cold compiler (cheng_cold --csg-in)
       ├─ cold_load_csg_v2_facts → BodyIR reconstruction
       ├─ codegen (ARM64/RISC-V/x86_64 dispatch)
       │    ├─ codegen_program → linkerless executable (exe path)
       │    └─ per-function codegen → relocatable .o (obj path)
       └─ emit
            ├─ Mach-O (arm64-apple-darwin)
            ├─ ELF64 (riscv64/x86_64/aarch64-linux-gnu)
            └─ COFF (x86_64-pc-windows-msvc)
```

## Facts Formats

### canonical `CHENG_CSG_V2`

Text-record object facts consumed by `system-link-exec --csg-in`.

```
MAGIC: CHENG_CSG_V2\n
R0001 target triple
R0002 object format
R0003 entry symbol
R0004 function records
R0005 instruction words
R0006 relocation records
```

### internal `CHENGCSG`

Section-binary BodyIR snapshot used only by explicit cold self-test commands.

```
HEADER (64 bytes): magic("CHENGCSG") version(2) target(32) abi ptr_width endian reserved
SECTION INDEX: section_count + N × (kind(4) + offset(8) + size(4))
FUNCTION SECTION (kind=1):
  fn_count + per-function:
    name (u32 len + utf8)
    params: count × (kind + size + slot_index)  [12 bytes/param]
    return_kind, frame_size
    ops: count × (kind + dst + a + b + c)  [20 bytes/op]
    slots: count × (kind + offset + size)  [12 bytes/slot]
    blocks: count × (op_start + op_count + term)  [12 bytes/block]
    terms: count × (kind + value + case_start + case_count + true_blk + false_blk)  [24 bytes/term]
    switches: count × (tag + block + term)  [12 bytes/case]
    calls, call_args, string_literals
STRING SECTION (kind=5)
TRAILER (32 bytes): section_count + hash(28)
```

## Codegen Architecture

### exe path (`codegen_program` + emit)
- Entry trampoline → reachability analysis → codegen → direct executable
- ARM64: `codegen_func` (ARM64 instruction encoder)
- RISC-V: `rv64_codegen_func` (RV64G instruction encoder)
- x86_64: `x64_codegen_func` (X64Code byte buffer → x64_pack_words → words)

### obj path (per-function codegen + object writer)
- Per-function codegen loop with `symbol_offset[]` tracking
- Patch resolution: internal BL → delta, external → relocation
- Object writers: `macho_write_object`, `elf64_write_object`, `coff_write_object`

## Failure Model

- Public CSG/object/exe paths hard-fail on unsupported body, unresolved non-external call, bad magic, truncated facts, missing provider export, wrong archive target, or unsupported target.
- No stub function, no mock provider, no fallback linker path is part of the CSG v2 contract.
- `ColdErrorRecoveryEnabled` remains an internal parser/import containment tool, not a success path for public artifact generation.

## Cross-platform Support

| Target | Format | Codegen | exe writer | obj writer |
|---|---|---|---|---|
| arm64-apple-darwin | Mach-O | codegen_func | output_direct_macho | macho_write_object |
| riscv64-linux-gnu | ELF64 | rv64_codegen_func | elf_write_exec | elf64_write_object |
| x86_64-linux-gnu | ELF64 | x64_codegen_func | elf_write_exec | elf64_write_object |
| aarch64-linux-gnu | ELF64 | codegen_func | elf_write_exec | elf64_write_object |

## Fixed-point Verification

Current gate: `tools/cold_csg_v2_roundtrip_test.sh` passes 732/732.

- Public `emit-cold-csg-v2` emits canonical `CHENG_CSG_V2`; cold reader emits object; `cc` link/run smoke exits 0.
- Internal `CHENGCSG` writer/reader fixed-point produces bit-identical objects for repeated reads of the same facts.
- Full Cheng `PrimaryObjectPlan` writer refresh is still blocked by `build-backend-driver` smoke mismatch, so it is not counted as full PrimaryObjectPlan pipeline closure.

## Key Files

| File | Role |
|---|---|
| `bootstrap/cheng_cold.c` | Cold compiler (reader + codegen + emit) |
| `bootstrap/cold_csg_v2_format.h` | Internal binary snapshot schema constants |
| `bootstrap/elf64_direct.h` | ELF64 object + executable writer |
| `bootstrap/x64_emit.h` | x86_64 instruction encoder |
| `bootstrap/rv64_emit.h` | RISC-V instruction encoder |
| `src/core/backend/primary_object_plan.cheng` | Cheng-side PrimaryObjectPlan facts writer source |
| `src/core/tooling/compiler_csg.cheng` | CSG type validation |
| `tools/cold_csg_v2_roundtrip_test.sh` | Automated CSG v2 roundtrip gate |

## Commands

```sh
# Generate canonical facts
cheng_cold emit-cold-csg-v2 --in:source.cheng --out:facts.csgv2

# Generate internal self-test facts
cheng_cold system-link-exec --in:source.cheng --emit:csg-v2 --out:facts.internal

# Consume facts → exe (all architectures)
cheng_cold system-link-exec --csg-in:facts.csgv2 --emit:exe \
  --target:arm64-apple-darwin --out:program

# Consume facts → obj (determinism check)
cheng_cold system-link-exec --csg-in:facts.csgv2 --emit:obj \
  --target:riscv64-unknown-linux-gnu --out:program.o

# Backend-only mode
cc -DCOLD_BACKEND_ONLY -o cheng_cold_backend bootstrap/cheng_cold.c
```
