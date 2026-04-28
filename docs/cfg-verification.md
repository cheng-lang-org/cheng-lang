# CFG Verification Guide

Verification commands for the general CFG codegen pipeline smokes.

## Prerequisites

- Working directory: `/root/cheng-lang`
- Compiler: `artifacts/backend_driver/cheng` or `artifacts/v3_backend_driver/cheng`
- Target: `arm64-apple-darwin` (or native target)

## Smoke Tests

### 1. cfg_body_ir_contract_smoke

Verifies BodyIR structures can be created and validated programmatically:
- 2 locals (int32, float64)
- 1 call op
- 1 basic block with return term
- Conditional branch (Cbr) and unconditional branch (Br) terms
- Field access on all BodyIR sub-types

```sh
artifacts/backend_driver/cheng system-link-exec \
  --root:/root/cheng-lang \
  --in:src/tests/cfg_body_ir_contract_smoke.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/cfg_body_ir_smoke \
  --report-out:/tmp/cfg_body_ir_smoke.report.txt

/tmp/cfg_body_ir_smoke
```

Expected output: ` cfg_body_ir_contract_smoke ok`
Expected exit code: 0
Expected report fields:
- `primary_object_missing_reasons=-`
- `primary_object_unsupported_function_count=0`
- `primary_object_direct=1` (when direct writer enabled)

### 2. cfg_lowering_smoke

Tests the lowering pipeline for a minimal multi-statement function with:
- `let x = GetValue()` -- let binding with call
- `if x <= 0: return 0` -- condition guard
- `return x` -- value return

```sh
artifacts/backend_driver/cheng system-link-exec \
  --root:/root/cheng-lang \
  --in:src/tests/cfg_lowering_smoke.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/cfg_lowering_smoke \
  --report-out:/tmp/cfg_lowering_smoke.report.txt

/tmp/cfg_lowering_smoke
```

Expected output: ` cfg_lowering_smoke ok`
Expected exit code: 0
Expected report fields:
- `lowering_function_count >= 3` (GetValue, RunGuard, main)
- `primary_object_missing_reasons=-`
- `primary_object_unsupported_function_count=0`

### 3. cfg_multi_stmt_smoke

Tests a function with multiple sequential statements:
- `let a = First()`
- `let b = Second(a)`
- `let result = UseBoth(a, b)`

```sh
artifacts/backend_driver/cheng system-link-exec \
  --root:/root/cheng-lang \
  --in:src/tests/cfg_multi_stmt_smoke.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/cfg_multi_stmt_smoke \
  --report-out:/tmp/cfg_multi_stmt_smoke.report.txt

/tmp/cfg_multi_stmt_smoke
```

Expected output: ` cfg_multi_stmt_smoke ok`
Expected exit code: 0
Expected report fields:
- `lowering_function_count >= 4` (First, Second, UseBoth, main)
- `primary_object_missing_reasons=-`
- `primary_object_unsupported_function_count=0`

### 4. cfg_result_project_smoke

Tests Result[T] projection in the general CFG:
- `let res = GetResult()`
- `if IsErr(res): return -1`
- `return Value(res)`

```sh
artifacts/backend_driver/cheng system-link-exec \
  --root:/root/cheng-lang \
  --in:src/tests/cfg_result_project_smoke.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/cfg_result_project_smoke \
  --report-out:/tmp/cfg_result_project_smoke.report.txt

/tmp/cfg_result_project_smoke
```

Expected output: ` cfg_result_project_smoke ok`
Expected exit code: 0
Expected report fields:
- `lowering_function_count >= 3` (GetResult, ProjectResult, main)
- `primary_object_missing_reasons=-`
- `primary_object_unsupported_function_count=0`

### 5. cfg_regression_gate_smoke

Compiles all existing fixtures through the pipeline API (from within Cheng):
- ordinary_zero_exit_fixture
- int32_const_return_direct_fixture
- float64_mul_backend_smoke
- function_task_contract_smoke

```sh
artifacts/backend_driver/cheng system-link-exec \
  --root:/root/cheng-lang \
  --in:src/tests/cfg_regression_gate_smoke.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/cfg_regression_gate_smoke \
  --report-out:/tmp/cfg_regression_gate_smoke.report.txt

/tmp/cfg_regression_gate_smoke
```

Expected output: ` cfg_regression_gate_smoke ok`
Expected exit code: 0

### 6. cfg_regression_gate.sh (Shell Variant)

A shell script that compiles and runs all fixtures, checking exit codes:

```sh
bash src/tests/cfg_regression_gate.sh
```

Expected output for each fixture: `PASS <fixture_name>`
Expected final line: `Results: 8 passed, 0 failed`

## Running the Full Regression Gate

To run all CFG-related verification smokes:

```sh
# Set root (adjust to your workspace)
ROOT=/root/cheng-lang

# Run each smoke individually
for smoke in cfg_body_ir_contract_smoke cfg_lowering_smoke \
              cfg_multi_stmt_smoke cfg_result_project_smoke \
              cfg_regression_gate_smoke; do
  echo "=== $smoke ==="
  $ROOT/artifacts/backend_driver/cheng system-link-exec \
    --root:$ROOT \
    --in:$ROOT/src/tests/${smoke}.cheng \
    --emit:exe \
    --target:arm64-apple-darwin \
    --out:/tmp/$smoke \
    --report-out:/tmp/$smoke.report.txt
  /tmp/$smoke
  echo "exit: $?"
done

# Run the shell regression gate
bash $ROOT/src/tests/cfg_regression_gate.sh
```

## Report Fields to Verify

For each smoke, the compilation report should show:

| Field | Expected |
|---|---|
| `primary_object_missing_reasons` | `-` (empty = ready) |
| `primary_object_unsupported_function_count` | `0` |
| `primary_object_direct` | `1` (direct writer enabled) |
| `primary_object_instruction_word_count` | > 0 |
| `provider_object_count` | `0` (no fallback providers) |

## Existing Fixtures Verified by Regression Gate

| Fixture | Expected Exit Code | Body Kind |
|---|---|---|
| `ordinary_zero_exit_fixture` | 0 | `return_zero_i32` |
| `int32_const_return_direct_fixture` | 7 | `return_i32_const_7` |
| `float64_mul_backend_smoke` | 0 | `return_f64_mul_args` |
| `function_task_contract_smoke` | 0 | multiple |
