#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
out_dir="$root/artifacts/v3_wasm_smokes"
seed_bin="$out_dir/cheng_v3_seed_wasm"
contract="$root/v3/bootstrap/stage1_bootstrap.cheng"

mkdir -p "$out_dir"

cc -std=c11 -O2 "$root/v3/bootstrap/cheng_v3_seed.c" -o "$seed_bin"

compile_one() {
  smoke_name="$1"
  src="$root/v3/src/tests/$smoke_name.cheng"
  out="$out_dir/$smoke_name.wasm"
  log="$out_dir/$smoke_name.compile.log"
  rm -f "$out" "$log"
  "$seed_bin" system-link-exec \
    --contract-in "$contract" \
    --root "$root/v3" \
    --in "$src" \
    --emit exe \
    --target wasm32-unknown-unknown \
    --out "$out" >"$log" 2>&1
  file "$out"
}

compile_one wasm_zero_smoke
compile_one wasm_internal_call_smoke
compile_one wasm_importc_noarg_i32_smoke

node - "$out_dir" <<'EOF'
const fs = require("fs");
const outDir = process.argv[2];

async function run(name, imports) {
  const wasm = fs.readFileSync(`${outDir}/${name}.wasm`);
  const { instance } = await WebAssembly.instantiate(wasm, imports);
  const rc = instance.exports.main();
  if (rc !== 0) {
    throw new Error(`${name} returned ${rc}`);
  }
  console.log(`${name}: ok`);
}

(async () => {
  await run("wasm_zero_smoke", { env: {} });
  await run("wasm_internal_call_smoke", { env: {} });
  await run("wasm_importc_noarg_i32_smoke", {
    env: {
      cheng_test_zero: () => 0
    }
  });
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
EOF

echo "v3 wasm smokes: ok"
