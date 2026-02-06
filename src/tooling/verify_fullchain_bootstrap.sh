#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"

case "$host_os/$host_arch" in
  Darwin/arm64)
    ;;
  *)
    echo "verify_fullchain_bootstrap skip: host=$host_os/$host_arch" 1>&2
    exit 2
    ;;
esac

obj_only="${CHENG_FULLCHAIN_OBJ_ONLY:-0}"
obj_fullspec_file="examples/backend_obj_fullspec.cheng"
if [ "$obj_only" = "1" ]; then
  if ! command -v codesign >/dev/null 2>&1; then
    echo "verify_fullchain_bootstrap skip: missing codesign" 1>&2
    exit 2
  fi
else
  if ! command -v cc >/dev/null 2>&1; then
    echo "verify_fullchain_bootstrap skip: missing cc" 1>&2
    exit 2
  fi
fi

stage2_self="artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2"
stage2_legacy="artifacts/backend_selfhost/backend_mvp_driver.stage2"
stage2="$stage2_self"
if [ "$obj_only" != "1" ] && [ ! -x "$stage2" ]; then
  stage2="$stage2_legacy"
fi
if [ ! -x "$stage2" ]; then
  auto_stage2="${CHENG_FULLCHAIN_AUTO_STAGE2:-1}"
  if [ "$auto_stage2" = "1" ]; then
    echo "== fullchain.auto_stage2.bootstrap =="
    seed_id=""
    if [ -f "dist/releases/current_id.txt" ]; then
      seed_id="$(cat dist/releases/current_id.txt | tr -d '\r\n')"
    elif [ -f "dist/backend/current_id.txt" ]; then
      seed_id="$(cat dist/backend/current_id.txt | tr -d '\r\n')"
    fi
    seed_tar=""
    seed_manifest=""
    if [ "$seed_id" != "" ]; then
      if [ -f "dist/releases/$seed_id/backend_release.tar.gz" ]; then
        seed_tar="dist/releases/$seed_id/backend_release.tar.gz"
        seed_manifest="dist/releases/$seed_id/release_manifest.json"
      else
        seed_tar="dist/backend/releases/$seed_id/backend_release.tar.gz"
        seed_manifest="dist/backend/releases/$seed_id/release_manifest.json"
      fi
    fi
    if [ "$seed_tar" != "" ] && [ -f "$seed_tar" ]; then
      # Verify the local dist seed (best-effort; can be made strict).
      if [ -f "$seed_manifest" ]; then
        set +e
        sh src/tooling/backend_release_verify.sh --manifest:"$seed_manifest" --bundle:"$seed_tar" --out-dir:"$(dirname "$seed_tar")"
        st="$?"
        set -e
        if [ "$st" -eq 0 ]; then
          :
        elif [ "$st" -eq 2 ]; then
          if [ "${CHENG_FULLCHAIN_REQUIRE_SEED_VERIFY:-0}" = "1" ]; then
            echo "[Error] seed verify skipped; set up openssl or disable CHENG_FULLCHAIN_REQUIRE_SEED_VERIFY" 1>&2
            exit 2
          fi
          echo "[warn] seed verify skipped (missing openssl?)" 1>&2
        else
          exit "$st"
        fi
      else
        echo "[warn] missing seed manifest; skipping signature verify: $seed_manifest" 1>&2
      fi

      out_dir="chengcache/backend_seed_fullchain_$$"
      mkdir -p "$out_dir"
      tar -xzf "$seed_tar" -C "$out_dir"
      seed_bin="$out_dir/backend_mvp_driver"
      if [ ! -f "$seed_bin" ]; then
        echo "[Error] seed tar missing backend_mvp_driver: $seed_tar" 1>&2
        exit 1
      fi
      chmod +x "$seed_bin" 2>/dev/null || true

      stage2_linker="self"
      if ! command -v codesign >/dev/null 2>&1; then
        stage2_linker="cc"
      fi

      CHENG_SELF_OBJ_BOOTSTRAP_STAGE0="$seed_bin" \
      CHENG_SELF_OBJ_BOOTSTRAP_LINKER="$stage2_linker" \
        sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
    fi
  fi

  stage2="$stage2_self"
  if [ "$obj_only" != "1" ] && [ ! -x "$stage2" ]; then
    stage2="$stage2_legacy"
  fi
  if [ ! -x "$stage2" ]; then
    echo "[Error] verify_fullchain_bootstrap missing stage2 backend driver: $stage2" 1>&2
    echo "[Hint] run one of:" 1>&2
    echo "  - sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh" 1>&2
    echo "  - sh src/tooling/verify_backend_ci_obj_only.sh --require-seed" 1>&2
    exit 1
  fi
fi

if [ "$obj_only" != "1" ] && [ ! -x ./stage1_runner ]; then
  echo "[Error] verify_fullchain_bootstrap missing ./stage1_runner (seed compiler)" 1>&2
  echo "[Hint] run: sh src/tooling/bootstrap.sh --fullspec" 1>&2
  exit 1
fi

export CHENG_BACKEND_DRIVER="$stage2"

out_dir="artifacts/fullchain"
bin_dir="$out_dir/bin"
mkdir -p "$bin_dir"
: >"$out_dir/mm_gaps.txt"

runtime_mode="${CHENG_FULLCHAIN_RUNTIME:-cheng}"
runtime_src="src/std/system_helpers_backend.cheng"
runtime_obj="chengcache/system_helpers.backend.cheng.o"

build_backend_runtime_obj() {
  if [ "$runtime_mode" != "cheng" ]; then
    return 0
  fi
  if [ ! -f "$runtime_src" ]; then
    echo "[Error] verify_fullchain_bootstrap missing runtime source: $runtime_src" 1>&2
    exit 1
  fi
  mkdir -p chengcache
  if [ -f "$runtime_obj" ] && [ "$runtime_src" -ot "$runtime_obj" ]; then
    return 0
  fi
  echo "== fullchain.build_backend_runtime_obj (cheng) =="
  env \
    CHENG_BACKEND_ALLOW_NO_MAIN=1 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_FRONTEND=mvp \
    CHENG_BACKEND_INPUT="$runtime_src" \
    CHENG_BACKEND_OUTPUT="$runtime_obj" \
    "$stage2" >/dev/null
  if [ ! -s "$runtime_obj" ]; then
    echo "[Error] verify_fullchain_bootstrap missing runtime obj: $runtime_obj" 1>&2
    exit 1
  fi
}

build_backend_runtime_obj

if [ "$obj_only" != "1" ]; then
  stage1_backend="$out_dir/stage1_runner.backend"

  echo "== fullchain.build_stage1_runner_backend =="
  sh src/tooling/build_stage1_runner_backend.sh --out:"$stage1_backend"

  backend_c="$out_dir/stage1_runner.backend.c"
  seed_c="$out_dir/stage1_runner.seed.c"

  hash256() {
    if command -v shasum >/dev/null 2>&1; then
      set -- $(shasum -a 256 "$1")
      printf '%s\n' "$1"
      return 0
    fi
    if command -v sha256sum >/dev/null 2>&1; then
      set -- $(sha256sum "$1")
      printf '%s\n' "$1"
      return 0
    fi
    echo ""
  }

normalized_hash() {
  in="$1"
  if ! grep -q "__cheng_case_" "$in" 2>/dev/null; then
    hash256 "$in"
    return 0
  fi
  if command -v shasum >/dev/null 2>&1; then
    sed -E 's/__cheng_case_[0-9]+/__cheng_case_X/g' "$in" | shasum -a 256 | awk '{print $1}'
    return 0
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sed -E 's/__cheng_case_[0-9]+/__cheng_case_X/g' "$in" | sha256sum | awk '{print $1}'
    return 0
  fi
  echo ""
}

  # 1) Generate stage1_runner.c using the backend-built stage1_runner.
  echo "== fullchain.stage1_runner_backend.emit_c =="
  if [ -f /tmp/stage1_runner.c ]; then mv /tmp/stage1_runner.c "/tmp/stage1_runner.c.prev.$$.backend"; fi
  CHENG_MM=off "$stage1_backend" >/dev/null
  if [ ! -f /tmp/stage1_runner.c ]; then
    echo "[Error] backend stage1_runner did not produce /tmp/stage1_runner.c" 1>&2
    exit 1
  fi
  cp /tmp/stage1_runner.c "$backend_c"

  # 2) Generate stage1_runner.c using the seed stage1_runner.
  echo "== fullchain.stage1_runner_seed.emit_c =="
  if [ -f /tmp/stage1_runner.c ]; then mv /tmp/stage1_runner.c "/tmp/stage1_runner.c.prev.$$.seed"; fi
  CHENG_MM=off ./stage1_runner >/dev/null
  if [ ! -f /tmp/stage1_runner.c ]; then
    echo "[Error] seed stage1_runner did not produce /tmp/stage1_runner.c" 1>&2
    exit 1
  fi
  cp /tmp/stage1_runner.c "$seed_c"

  # 3) Compare normalized hashes.
  echo "== fullchain.stage1_runner.emit_c.compare =="
  h_backend="$(normalized_hash "$backend_c")"
  h_seed="$(normalized_hash "$seed_c")"
  if [ "$h_backend" = "" ] || [ "$h_seed" = "" ]; then
    echo "verify_fullchain_bootstrap skip: missing sha256 tool" 1>&2
    exit 2
  fi
  if [ "$h_backend" != "$h_seed" ]; then
    echo "[verify_fullchain_bootstrap] stage1_runner.c mismatch: $h_backend vs $h_seed" 1>&2
    diff -u "$seed_c" "$backend_c" || true
    exit 1
  fi

  # 4) Run stage1 fullspec using the backend-built stage1_runner.
  echo "== fullchain.stage1_fullspec (frontend=backend stage1_runner) =="
  sh src/tooling/verify_stage1_fullspec.sh --frontend:"$stage1_backend"
fi

if [ "$obj_only" = "1" ]; then
  # Obj-only semantic gate: compile+run backend fullspec subset via backend driver (frontend=stage1).
  # Keep acceptance stable ("fullspec ok") while avoiding unsupported full stage1 fullspec constructs.
  if [ ! -f "$obj_fullspec_file" ]; then
    echo "[Error] verify_fullchain_bootstrap missing obj-only fullspec file: $obj_fullspec_file" 1>&2
    exit 1
  fi
  echo "== fullchain.stage1_fullspec (obj-only, backend: $obj_fullspec_file) =="
  CHENG_BACKEND_LINKER=self \
  CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
  CHENG_BACKEND_NO_RUNTIME_C=1 \
  sh src/tooling/verify_stage1_fullspec.sh \
    --backend:obj \
    --mm:off \
    --file:"$obj_fullspec_file" \
    --name:"$out_dir/stage1_fullspec_obj" \
    --log:"$out_dir/stage1_fullspec_obj.out"
fi

# 5) Build and smoke key tools using the backend (stage2 driver + stage1 frontend).
# Prefer CHENG_MM=orc; fall back to off and record a gap when needed.

build_tool() {
  src="$1"
  out="$2"
  tool_name="$(basename "$out")"

  echo "== fullchain.build.tool: $tool_name =="

  runtime_env=""
  if [ "$runtime_mode" = "cheng" ]; then
    runtime_env="CHENG_BACKEND_NO_RUNTIME_C=1 CHENG_BACKEND_RUNTIME_OBJ=$runtime_obj"
  fi
  if [ "$obj_only" = "1" ]; then
    runtime_env="$runtime_env CHENG_BACKEND_LINKER=self"
  fi

  if CHENG_MM=orc \
    CHENG_BACKEND_VALIDATE=1 \
    env $runtime_env \
    sh src/tooling/chengb.sh "$src" --frontend:stage1 --emit:exe --out:"$out" >/dev/null; then
    :
  else
    echo "[warn] $tool_name build failed with CHENG_MM=orc; retry with CHENG_MM=off" 1>&2
    if CHENG_MM=off \
      CHENG_BACKEND_VALIDATE=1 \
      env $runtime_env \
      sh src/tooling/chengb.sh "$src" --frontend:stage1 --emit:exe --out:"$out" >/dev/null; then
      printf "%s\n" "$tool_name: orc->off" >>"$out_dir/mm_gaps.txt"
    else
      echo "[Error] tool build failed: $tool_name" 1>&2
      exit 1
    fi
  fi

  if [ ! -x "$out" ]; then
    echo "[Error] missing tool output: $out" 1>&2
    exit 1
  fi

  "$out" --help >/dev/null 2>&1 || {
    echo "[Error] tool --help failed: $out" 1>&2
    exit 1
  }
}

build_tool "src/tooling/cheng_pkg_source.cheng" "$bin_dir/cheng_pkg_source"
build_tool "src/tooling/cheng_pkg.cheng" "$bin_dir/cheng_pkg"
build_tool "src/tooling/cheng_storage.cheng" "$bin_dir/cheng_storage"

if [ -f "$out_dir/mm_gaps.txt" ]; then
  echo "== fullchain.mm_gaps ==" 1>&2
  cat "$out_dir/mm_gaps.txt" 1>&2
fi

echo "verify_fullchain_bootstrap ok"
