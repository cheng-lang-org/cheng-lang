# Backend Memory-Exe + Hotpatch Contract (Trampoline/Append-Only)

This document freezes the minimum Dev hotpatch contract for the Memory-Exe
pipeline after migrating to:

- global trampoline route (thunk indirection)
- append-only code pool commits
- full-program layout-hash fallback restart

- `mem_contract.version=2`

## MemImage Schema

- `mem_image.schema.version=1`

### Fields

- `mem_image.field.target`
- `mem_image.field.format`
- `mem_image.field.entry`
- `mem_image.field.sections`
- `mem_image.field.symbols`
- `mem_image.field.relocations`
- `mem_image.field.runtime_blob`
- `mem_image.field.link_mode`

### Invariants

- `mem_image.invariant.section_order_stable`
- `mem_image.invariant.symbol_id_stable`
- `mem_image.invariant.relocation_apply_before_emit`
- `mem_image.invariant.entry_symbol_resolved`
- `mem_image.invariant.output_transactional_write`

## PatchMeta Schema

- `patch_meta.schema.version=2`

### Fields

- `patch_meta.field.thunk_id`
- `patch_meta.field.target_slot_fileoff_or_memoff`
- `patch_meta.field.code_pool_offset`
- `patch_meta.field.layout_hash`
- `patch_meta.field.commit_epoch`
- `patch_meta.field.abi`
- `patch_meta.field.symbol`
- `patch_meta.field.hash`

### Invariants

- `patch_meta.invariant.thunk_id_stable`
- `patch_meta.invariant.commit_append_or_restart_only`
- `patch_meta.invariant.layout_hash_mismatch_restart`
- `patch_meta.invariant.rollback_before_slot_switch`
- `patch_meta.invariant.host_pid_stable`
