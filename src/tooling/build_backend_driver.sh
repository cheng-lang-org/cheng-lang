#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/build_backend_driver.sh [--name:<binName>]

Builds the self-hosted backend driver binary (runs on the host):
  - Preferred (Darwin): Cheng backend self-link (no `cc`)
  - Fallback: Cheng -> .o -> cc link

Output:
  ./<binName> (default: backend_mvp_driver)

Env:
  CHENG_BACKEND_BUILD_DRIVER_LINKER=self|cc (default: auto)
EOF
}

name="backend_mvp_driver"
while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --name:*)
      name="${1#--name:}"
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"

target=""
case "$host_os" in
  Darwin)
    case "$host_arch" in
      arm64) target="arm64-apple-darwin" ;;
    esac
    ;;
  Linux)
    case "$host_arch" in
      aarch64|arm64) target="aarch64-unknown-linux-gnu" ;;
    esac
    ;;
esac

extra_ldflags=""
if [ "$host_os" = "Linux" ]; then
  extra_ldflags="-lm"
fi

run_with_timeout() {
  seconds="$1"
  shift
  perl -e '
    use POSIX qw(setsid WNOHANG);
    my $timeout = shift;
    my $pid = fork();
    if (!defined $pid) { exit 127; }
    if ($pid == 0) {
      setsid();
      exec @ARGV;
      exit 127;
    }
    my $end = time + $timeout;
    while (1) {
      my $res = waitpid($pid, WNOHANG);
      if ($res == $pid) {
        exit ($? >> 8);
      }
      if (time >= $end) {
        kill "TERM", -$pid;
        select(undef, undef, undef, 0.5);
        kill "KILL", -$pid;
        exit 124;
      }
      select(undef, undef, undef, 0.1);
    }
  ' "$seconds" "$@"
}

driver_sanity_ok() {
  bin="$1"
  if [ ! -x "$bin" ]; then
    return 1
  fi
  cmd="$bin"
  case "$cmd" in
    /*|./*|../*) ;;
    *)
      cmd="./$cmd"
      ;;
  esac
  set +e
  run_with_timeout 5 "$cmd" --help >/dev/null 2>&1
  status=$?
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

try_seed_driver_fallback() {
  seed_id="${CHENG_BACKEND_SEED_ID:-}"
  if [ "$seed_id" = "" ] && [ -f "$root/dist/releases/current_id.txt" ]; then
    seed_id="$(cat "$root/dist/releases/current_id.txt" | tr -d '\r\n')"
  fi
  if [ "$seed_id" = "" ] && [ -f "$root/dist/backend/current_id.txt" ]; then
    seed_id="$(cat "$root/dist/backend/current_id.txt" | tr -d '\r\n')"
  fi
  seed_tar="${CHENG_BACKEND_SEED_TAR:-}"
  if [ "$seed_tar" = "" ] && [ "$seed_id" != "" ]; then
    for try_tar in \
      "$root/dist/releases/$seed_id/backend_release.tar.gz" \
      "$root/dist/backend/releases/$seed_id/backend_release.tar.gz"; do
      if [ -f "$try_tar" ]; then
        seed_tar="$try_tar"
        break
      fi
    done
  fi
  if [ "$seed_tar" = "" ] || [ ! -f "$seed_tar" ]; then
    return 1
  fi

  seed_out_dir="$root/chengcache/backend_seed_driver/${seed_id:-local}"
  mkdir -p "$seed_out_dir"
  set +e
  tar -xzf "$seed_tar" -C "$seed_out_dir" >/dev/null 2>&1
  tar_status="$?"
  set -e
  if [ "$tar_status" -ne 0 ]; then
    return 1
  fi

  seed_bin="$seed_out_dir/backend_mvp_driver"
  chmod +x "$seed_bin" 2>/dev/null || true
  if ! driver_sanity_ok "$seed_bin"; then
    return 1
  fi

  cp -f "$seed_bin" "$name"
  chmod +x "$name" 2>/dev/null || true
  if ! driver_sanity_ok "$name"; then
    return 1
  fi
  echo "build_backend_driver ok (seed_fallback)"
  return 0
}

# Pick linker strategy for the fast selfhost path.
linker_mode="${CHENG_BACKEND_BUILD_DRIVER_LINKER:-}"
if [ -z "$linker_mode" ]; then
  if [ "$host_os" = "Darwin" ] && command -v codesign >/dev/null 2>&1; then
    linker_mode="self"
  else
    linker_mode="cc"
  fi
fi
case "$linker_mode" in
  self|cc) ;;
  *)
    echo "[Error] invalid CHENG_BACKEND_BUILD_DRIVER_LINKER=$linker_mode (expected self|cc)" 1>&2
    exit 2
    ;;
esac

# Fast path (optional): self-host rebuild via an existing backend driver.
# Default is off to keep local rebuilds stable during source/layout migrations.
selfhost="${CHENG_BACKEND_BUILD_DRIVER_SELFHOST:-0}"
stage0=""
if [ -n "${CHENG_BACKEND_BUILD_DRIVER_STAGE0:-}" ]; then
  stage0="${CHENG_BACKEND_BUILD_DRIVER_STAGE0}"
elif [ -x "$root/artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2" ]; then
  stage0="$root/artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2"
elif [ -x "$root/artifacts/backend_selfhost/backend_mvp_driver.stage2" ]; then
  stage0="$root/artifacts/backend_selfhost/backend_mvp_driver.stage2"
else
  stage0="$root/backend_mvp_driver"
fi

if [ "$selfhost" != "0" ] && [ -n "$target" ] && [ -x "$stage0" ]; then
  if [ "$linker_mode" = "self" ] && [ "$host_os" = "Darwin" ]; then
    runtime_src="src/std/system_helpers_backend.cheng"
    runtime_obj="chengcache/system_helpers.backend.cheng.${target}.o"
    tmp_bin="${name}.tmp"

    mkdir -p chengcache
    : >"$runtime_obj.build.log"
    set +e
    env \
      CHENG_BACKEND_ALLOW_NO_MAIN=1 \
      CHENG_BACKEND_WHOLE_PROGRAM=1 \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET="$target" \
      CHENG_BACKEND_FRONTEND=mvp \
      CHENG_BACKEND_INPUT="$runtime_src" \
      CHENG_BACKEND_OUTPUT="$runtime_obj" \
      "$stage0" >"$runtime_obj.build.log" 2>&1
    rt_status=$?
    set -e
    if [ "$rt_status" -eq 0 ] && [ -s "$runtime_obj" ]; then
      : >"chengcache/${name}.selfhost.self.link.log"
      set +e
      env \
        CHENG_BACKEND_VALIDATE=1 \
        CHENG_BACKEND_LINKER=self \
        CHENG_BACKEND_NO_RUNTIME_C=1 \
        CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
        CHENG_BACKEND_EMIT=exe \
        CHENG_BACKEND_TARGET="$target" \
        CHENG_BACKEND_FRONTEND=stage1 \
        CHENG_BACKEND_INPUT="src/backend/tooling/backend_driver.cheng" \
        CHENG_BACKEND_OUTPUT="$tmp_bin" \
        "$stage0" >"chengcache/${name}.selfhost.self.link.log" 2>&1
      status=$?
      set -e
      if [ "$status" -eq 0 ] && [ -x "$tmp_bin" ]; then
        if driver_sanity_ok "$tmp_bin"; then
          mv "$tmp_bin" "$name"
          if [ -s "$tmp_bin.o" ]; then
            mv "$tmp_bin.o" "$name.o"
          fi
          echo "build_backend_driver ok (selfhost, self-link)"
          exit 0
        fi
        rm -f "$tmp_bin" "$tmp_bin.o"
        echo "[Warn] build_backend_driver: self-link produced non-runnable binary; falling back to cc link" 1>&2
      fi
      echo "[Warn] build_backend_driver: self-link failed (status=$status); falling back to cc link" 1>&2
    else
      echo "[Warn] build_backend_driver: runtime obj build failed (status=$rt_status); falling back to cc link" 1>&2
    fi
  fi

  if ! command -v cc >/dev/null 2>&1; then
    echo "[Warn] build_backend_driver: missing cc; falling back to stage1 C" 1>&2
  else
    obj_path="chengcache/${name}.selfhost.o"
    tmp_bin="${name}.tmp"
    rm -f "$obj_path" "$tmp_bin"
    set +e
  env \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="src/backend/tooling/backend_driver.cheng" \
    CHENG_BACKEND_OUTPUT="$obj_path" \
    "$stage0"
  status=$?
  set -e
  if [ "$status" -eq 0 ] && [ -s "$obj_path" ]; then
    set +e
    cc -O2 \
      -I runtime/include \
      -I src/std \
      -I src/core \
      "$obj_path" \
      src/runtime/native/system_helpers.c \
      -o "$tmp_bin" \
      $extra_ldflags
    link_status=$?
    set -e
    if [ "$link_status" -eq 0 ] && driver_sanity_ok "$tmp_bin"; then
      mv "$tmp_bin" "$name"
      echo "build_backend_driver ok (selfhost)"
      exit 0
    fi
    rm -f "$tmp_bin"
    echo "[Warn] build_backend_driver: cc-link failed (status=${link_status:-1}) or produced non-runnable binary; falling back" 1>&2
  fi
  rm -f "$obj_path" "$tmp_bin"
  echo "[Warn] build_backend_driver: selfhost failed (status=$status); falling back to stage1 C" 1>&2
  fi
fi

# Practical fallback in migration windows: use a verified release seed driver.
if try_seed_driver_fallback; then
  exit 0
fi

# Slow fallback: Cheng -> C (stage1_runner) -> cc link.
# Force C backend here to avoid recursive `backend_driver_path -> build_backend_driver -> chengc(obj)` loops.
c_path="chengcache/${name}.c"
CHENG_EMIT_C_MODULES=0 src/tooling/chengc.sh src/backend/tooling/backend_driver.cheng \
  --name:"$name" \
  --backend:c \
  --emit-c \
  --c-out:"$c_path"
tmp_bin="${name}.tmp"
cc -O2 \
  -I runtime/include \
  -I src/std \
  -I src/core \
  "$c_path" \
  src/runtime/native/system_helpers.c \
  -o "$tmp_bin" \
  $extra_ldflags

if driver_sanity_ok "$tmp_bin"; then
  mv "$tmp_bin" "$name"
  echo "build_backend_driver ok (stage1_c)"
  exit 0
fi
rm -f "$tmp_bin"
echo "[Error] build_backend_driver: stage1 C fallback produced non-runnable binary" 1>&2
exit 1
