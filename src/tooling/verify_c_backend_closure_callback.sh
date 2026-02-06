#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_c_backend_closure_callback.sh [--example:<path>] [--impl:<path>] [--name:<exe>] [--out-dir:<dir>]

Notes:
  - Verifies closure lowering + callback trampoline in direct C backend.
  - Builds the Cheng module with the C frontend, then links with the C impl.
EOF
}

example="${CHENG_C_CLOSURE_EXAMPLE:-examples/c_backend_closure_callback.cheng}"
impl="${CHENG_C_CLOSURE_IMPL:-examples/c_backend_closure_callback.c}"
name="${CHENG_C_CLOSURE_NAME:-c_backend_closure_callback}"
out_dir="${CHENG_C_CLOSURE_OUT_DIR:-chengcache/c_backend_closure_callback}"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --example:*)
      example="${1#--example:}"
      ;;
    --impl:*)
      impl="${1#--impl:}"
      ;;
    --name:*)
      name="${1#--name:}"
      ;;
    --out-dir:*)
      out_dir="${1#--out-dir:}"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

example_path="$example"
case "$example" in
  /*) ;;
  *) example_path="$PWD/$example" ;;
esac
impl_path="$impl"
case "$impl" in
  /*) ;;
  *) impl_path="$PWD/$impl" ;;
esac

if [ ! -f "$example_path" ]; then
  echo "[Error] missing example: $example" 1>&2
  exit 2
fi
if [ ! -f "$impl_path" ]; then
  echo "[Error] missing C impl: $impl" 1>&2
  exit 2
fi

frontend="${CHENG_C_FRONTEND:-}"
if [ -z "$frontend" ]; then
  stage1_fresh="0"
  if [ -x "./stage1_runner" ]; then
    stage1_fresh="1"
    for src in \
      src/stage1/frontend_stage1.cheng \
      src/stage1/frontend_lib.cheng \
      src/stage1/parser.cheng \
      src/stage1/lexer.cheng \
      src/stage1/ast.cheng \
      src/stage1/semantics.cheng \
      src/stage1/monomorphize.cheng \
      src/stage1/ownership.cheng \
      src/stage1/c_profile_lowering.cheng \
      src/stage1/c_codegen.cheng \
      src/stage1/diagnostics.cheng; do
      if [ -f "$src" ] && [ "$src" -nt "./stage1_runner" ]; then
        stage1_fresh="0"
        break
      fi
    done
  fi
  if [ -x "./stage1_runner" ]; then
    if [ "$stage1_fresh" != "1" ]; then
      echo "[Warn] ./stage1_runner is older than stage1 sources; consider src/tooling/bootstrap.sh" 1>&2
    fi
    frontend="./stage1_runner"
  else
    echo "[Error] stage1_runner is required for C backend" 1>&2
    exit 2
  fi
fi
if [ ! -x "$frontend" ]; then
  echo "[Error] missing C frontend: $frontend" 1>&2
  exit 2
fi

cc="${CC:-cc}"
cflags="${CFLAGS:-"-O2"}"
cflags="$cflags -Wno-incompatible-pointer-types -Wno-incompatible-pointer-types-discards-qualifiers"
runtime_inc="${CHENG_RT_INC:-${CHENG_RUNTIME_INC:-runtime/include}}"

mkdir -p "$out_dir"
rm -f "$out_dir/$name" "$name"

c_out="$out_dir/$name.c"
obj_out="$out_dir/$name.o"
impl_obj="$out_dir/$name.ffi.o"
helpers_obj="$out_dir/$name.system_helpers.o"

rm -f "$c_out" "$obj_out" "$impl_obj" "$helpers_obj"

CHENG_C_MODULE="$example_path" "$frontend" --mode:c --file:"$example_path" --out:"$c_out"

"$cc" $cflags -I"$runtime_inc" -Isrc/runtime/native -c "$c_out" -o "$obj_out"
"$cc" $cflags -I"$runtime_inc" -Isrc/runtime/native -c "$impl_path" -o "$impl_obj"
"$cc" $cflags -I"$runtime_inc" -Isrc/runtime/native -c src/runtime/native/system_helpers.c -o "$helpers_obj"
"$cc" $cflags "$obj_out" "$impl_obj" "$helpers_obj" -o "$name"

set +e
"./$name"
code="$?"
set -e
if [ "$code" -ne 7 ]; then
  echo "[Error] unexpected exit code: $code (expected 7)" 1>&2
  exit 2
fi

mv -f "$name" "$out_dir/$name"

echo "verify_c_backend_closure_callback ok"
