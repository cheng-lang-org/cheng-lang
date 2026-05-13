# CSG v2 Architecture

Cheng 编译器的冷启动后端架构。Cheng 完整编译器产出 CSG v2 二进制事实文件，C 冷编译器消费事实并产出可执行文件。

## Pipeline

```
Cheng Source
  └─ Cheng full compiler (artifacts/backend_driver/cheng)
       └─ parser → typed_expr → lowering → PrimaryObjectPlan
            └─ PrimaryBodyIrEmitCsgV2BinaryFacts (primary_object_plan.cheng)
                 └─ CSG v2 binary facts (CHENGCSG magic, 280B–489KB)

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

## Binary Format (cold_csg_v2_format.h)

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

## Error Recovery

- Per-function `setjmp/longjmp`: catches `die()` during codegen, emits stub function
- `ColdErrorRecoveryEnabled` flag: SIGSEGV/BUS handler → longjmp to recovery point
- External symbols: auto-registered as `__ext_fn_N` when call target missing
- Return type mapping: `return_kind` → type string for `symbols_add_fn`

## Cross-platform Support

| Target | Format | Codegen | exe writer | obj writer |
|---|---|---|---|---|
| arm64-apple-darwin | Mach-O | codegen_func | output_direct_macho | macho_write_object |
| riscv64-linux-gnu | ELF64 | rv64_codegen_func | elf_write_exec | elf64_write_object |
| x86_64-linux-gnu | ELF64 | x64_codegen_func | elf_write_exec | elf64_write_object |
| aarch64-linux-gnu | ELF64 | codegen_func | elf_write_exec | elf64_write_object |

## Fixed-point Verification

C writer (`cold_emit_csg_v2_facts` in cheng_cold.c) and Cheng writer
(`PrimaryBodyIrEmitCsgV2BinaryFacts` in primary_object_plan.cheng) produce
**bit-identical** facts (MD5 match) for the same input. Cold reader consumes
both identically, producing **bit-identical** executables.

## Key Files

| File | Role |
|---|---|
| `bootstrap/cheng_cold.c` | Cold compiler (reader + codegen + emit) |
| `bootstrap/cold_csg_v2_format.h` | Binary format schema constants |
| `bootstrap/elf64_direct.h` | ELF64 object + executable writer |
| `bootstrap/x64_emit.h` | x86_64 instruction encoder |
| `bootstrap/rv64_emit.h` | RISC-V instruction encoder |
| `src/core/backend/primary_object_plan.cheng` | Cheng-side facts writer |
| `src/core/tooling/compiler_csg.cheng` | CSG type validation |
| `tools/cold_csg_v2_roundtrip_test.sh` | Automated roundtrip test (34 checks) |

## Commands

```sh
# Generate facts (C cold compiler)
cheng_cold emit-cold-csg-v2 --in:source.cheng --out:facts.bin

# Generate facts (Cheng toolchain sidecar)
artifacts/backend_driver/cheng system-link-exec --in:source.cheng \
  --emit:exe --cold-csg-out:facts.bin

# Consume facts → exe (all architectures)
cheng_cold system-link-exec --csg-in:facts.bin --emit:exe \
  --target:arm64-apple-darwin --out:program

# Consume facts → obj (determinism check)
cheng_cold system-link-exec --csg-in:facts.bin --emit:obj \
  --target:riscv64-unknown-linux-gnu --out:program.o

# Backend-only mode
cc -DCOLD_BACKEND_ONLY -o cheng_cold_backend bootstrap/cheng_cold.c
```
