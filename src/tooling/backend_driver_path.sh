#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

to_abs() {
  p="$1"
  case "$p" in
    /*)
      ;; 
    *)
      p="$root/$p"
      ;;
  esac
  d="$(CDPATH= cd -- "$(dirname -- "$p")" && pwd)"
  printf "%s/%s\n" "$d" "$(basename -- "$p")"
}

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
  set +e
  run_with_timeout 5 "$bin" --help >/dev/null 2>&1
  status=$?
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

driver="${CHENG_BACKEND_DRIVER:-}"
if [ "$driver" != "" ]; then
  abs="$(to_abs "$driver")"
  if [ ! -x "$abs" ]; then
    echo "[Error] CHENG_BACKEND_DRIVER is not executable: $abs" 1>&2
    exit 1
  fi
  if ! driver_sanity_ok "$abs"; then
    echo "[Error] CHENG_BACKEND_DRIVER is not runnable: $abs" 1>&2
    exit 1
  fi
  printf "%s\n" "$abs"
  exit 0
fi

is_stale() {
  bin="$1"
  if [ ! -x "$bin" ]; then
    return 0
  fi
  if find "$root/src/backend" "$root/src/stage1" "$root/src/std" "$root/src/core" "$root/src/system" \
      -type f \( -name '*.cheng' -o -name '*.c' -o -name '*.h' \) \
      -newer "$bin" -print -quit | grep -q .; then
    return 0
  fi
  return 1
}

abs_default="$root/backend_mvp_driver"

# Fast path: use the local rebuilt driver if it's fresh.
if [ -x "$abs_default" ] && ! is_stale "$abs_default"; then
  if driver_sanity_ok "$abs_default"; then
    printf "%s\n" "$abs_default"
    exit 0
  fi
  echo "[Warn] backend_driver_path: local backend_mvp_driver is not runnable, rebuilding" 1>&2
fi

# Prefer a self-hosted stage2 driver when available.
stage2_self="$root/artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2"
stage2_legacy="$root/artifacts/backend_selfhost/backend_mvp_driver.stage2"
stage2=""
if [ -x "$stage2_self" ]; then
  stage2="$stage2_self"
elif [ -x "$stage2_legacy" ]; then
  stage2="$stage2_legacy"
fi
stale_stage2=""
if [ "$stage2" != "" ]; then
  if driver_sanity_ok "$stage2"; then
    if ! is_stale "$stage2"; then
      printf "%s\n" "$stage2"
      exit 0
    fi
    stale_stage2="$stage2"
  else
    echo "[Warn] backend_driver_path: stage2 is not runnable: $stage2" 1>&2
  fi
fi

# Best-effort: build the self-hosted stage2 driver from a dist seed (no `cc` required on Darwin).
# Only do this when we don't already have any stage2 driver; otherwise it can be slow and redundant.
host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"
case "$host_os/$host_arch" in
  Darwin/arm64)
    if [ -x "$stage2_self" ] || [ -x "$stage2_legacy" ]; then
      :
    else
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

      if [ "$seed_tar" != "" ] && [ -f "$seed_tar" ]; then
        seed_out_dir="$root/chengcache/backend_seed_driver/${seed_id:-local}"
        mkdir -p "$seed_out_dir"
        set +e
        tar -xzf "$seed_tar" -C "$seed_out_dir" >/dev/null 2>&1
        tar_status="$?"
        set -e
        if [ "$tar_status" -ne 0 ]; then
          echo "[Warn] backend_driver_path: failed to extract seed tar: $seed_tar" 1>&2
        fi
        seed_path="$seed_out_dir/backend_mvp_driver"
        chmod +x "$seed_path" 2>/dev/null || true
        if [ -x "$seed_path" ]; then
          set +e
          CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL=0 \
          CHENG_SELF_OBJ_BOOTSTRAP_STAGE0="$seed_path" \
          CHENG_SELF_OBJ_BOOTSTRAP_LINKER=self \
          CHENG_SELF_OBJ_BOOTSTRAP_RUNTIME=cheng \
            sh "$root/src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh" >/dev/null 2>&1
          selfhost_status="$?"
          set -e
          if [ "$selfhost_status" -ne 0 ]; then
            echo "[Warn] backend_driver_path: selfhost from seed failed (status=$selfhost_status): $seed_path" 1>&2
          fi
        fi
      fi

      stage2_self="$root/artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2"
      if [ -x "$stage2_self" ] && ! is_stale "$stage2_self"; then
        if driver_sanity_ok "$stage2_self"; then
          printf "%s\n" "$stage2_self"
          exit 0
        fi
        echo "[Warn] backend_driver_path: seed-rebuilt stage2 is not runnable: $stage2_self" 1>&2
      fi
    fi
    ;;
esac

rebuild_default="0"
if [ ! -x "$abs_default" ]; then
  rebuild_default="1"
else
  if is_stale "$abs_default"; then
    rebuild_default="1"
  elif ! driver_sanity_ok "$abs_default"; then
    rebuild_default="1"
  fi
fi

if [ "$rebuild_default" = "1" ]; then
  mkdir -p "$root/chengcache"
  rebuild_log="$root/chengcache/backend_driver_path.rebuild.log"
  set +e
  CHENG_BACKEND_BUILD_DRIVER_SELFHOST="${CHENG_BACKEND_DRIVER_PATH_SELFHOST:-0}" \
  CHENG_BACKEND_BUILD_DRIVER_LINKER="${CHENG_BACKEND_DRIVER_PATH_LINKER:-cc}" \
    sh src/tooling/build_backend_driver.sh --name:backend_mvp_driver >"$rebuild_log" 2>&1
  rebuild_status="$?"
  set -e
  if [ "$rebuild_status" -ne 0 ]; then
    echo "[Warn] backend_driver_path: local backend_mvp_driver rebuild failed (status=$rebuild_status)" 1>&2
    if [ "${CHENG_BACKEND_DRIVER_PATH_DEBUG:-0}" = "1" ]; then
      tail -n 80 "$rebuild_log" 1>&2 || true
    fi
  fi
fi
if [ -x "$abs_default" ] && driver_sanity_ok "$abs_default"; then
  printf "%s\n" "$abs_default"
  exit 0
fi

if [ "$stage2" != "" ] && ! is_stale "$stage2" && driver_sanity_ok "$stage2"; then
  printf "%s\n" "$stage2"
  exit 0
fi

if [ "$stale_stage2" != "" ] && driver_sanity_ok "$stale_stage2"; then
  echo "[Warn] backend_driver_path: falling back to stale stage2 driver: $stale_stage2" 1>&2
  printf "%s\n" "$stale_stage2"
  exit 0
fi

if [ ! -x "$abs_default" ]; then
  echo "[Error] missing backend driver: $abs_default" 1>&2
  exit 1
fi
if ! driver_sanity_ok "$abs_default"; then
  echo "[Error] backend driver exists but is not runnable: $abs_default" 1>&2
  exit 1
fi
printf "%s\n" "$abs_default"
