#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} verify_backend_selfhost_100ms_host [--help]

Env:
  SELFHOST_100MS_BASELINE=<path>          default: src/tooling/selfhost_perf_100ms_host.env
  SELFHOST_100MS_HOST_ONLY=<tag>          default: darwin_arm64
  SELFHOST_100MS_TRACK=quick|full          default: quick
  SELFHOST_100MS_ITERS=<N>                default: 5 (quick) or SELFHOST_100MS_QUICK_ITERS (quick track)
  SELFHOST_100MS_P95_MS=<ms>              default: 100 (quick) or SELFHOST_100MS_QUICK_P95_MS (quick track)
  SELFHOST_100MS_P99_MS=<ms>              default: 120 (quick) or SELFHOST_100MS_QUICK_P99_MS (quick track)
  SELFHOST_100MS_ENFORCE_ON_HOST=<0|1>    default: 1
  SELFHOST_100MS_QUICK_ITERS=<N>          quick default: 3
  SELFHOST_100MS_QUICK_P95_MS=<ms>        quick default: 30000
  SELFHOST_100MS_QUICK_P99_MS=<ms>        quick default: 40000
  SELFHOST_100MS_QUICK_ENFORCE_ON_HOST=<0|1> default: 0
  SELFHOST_100MS_QUICK_REUSE=<0|1>        quick default: 1
  SELFHOST_100MS_QUICK_TIMEOUT=<seconds>   quick compile timeout default: 60
  SELFHOST_100MS_FULL_ITERS=<N>           full default: 3
  SELFHOST_100MS_FULL_P95_MS=<ms>         full default: 100
  SELFHOST_100MS_FULL_P99_MS=<ms>         full default: 120
  SELFHOST_100MS_FULL_ENFORCE_ON_HOST=<0|1> default: 1
  SELFHOST_100MS_FULL_REUSE=<0|1>         full default: 0
  SELFHOST_100MS_FULL_TIMEOUT=<seconds>    full compile timeout default: 60
  SELFHOST_100MS_REPORT=<path>            default: artifacts/backend_selfhost_100ms_host/report.tsv
  SELFHOST_STRICT_REBUILD=<0|1>           override default by track
  SELFHOST_STAGE1_FULL_REBUILD=<0|1>      override default by track
  SELFHOST_STAGE1_REQUIRE_REBUILD=<0|1>   override default by track

Notes:
  - This gate is host-only and intended for dedicated perf runners.
  - Non-target hosts are report-only (exit 0).
EOF
}

for arg in "$@"; do
  case "$arg" in
    --help|-h)
      usage
      exit 0
      ;;
  esac
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

baseline_file="${SELFHOST_100MS_BASELINE:-src/tooling/selfhost_perf_100ms_host.env}"
if [ ! -f "$baseline_file" ]; then
  echo "[selfhost_100ms] baseline file not found: $baseline_file" >&2
  exit 1
fi
# shellcheck disable=SC1090
. "$baseline_file"

host_os="$(uname -s 2>/dev/null | tr 'A-Z' 'a-z')"
host_arch="$(uname -m 2>/dev/null | tr 'A-Z' 'a-z')"
host_tag="${host_os}_${host_arch}"
if [ "$host_os" = "darwin" ] && [ "$host_arch" = "arm64" ]; then
  host_tag="darwin_arm64"
fi

host_only="${SELFHOST_100MS_HOST_ONLY:-darwin_arm64}"
track="${SELFHOST_100MS_TRACK:-${SELFHOST_100MS_FAST_TRACK:-quick}}"
case "$track" in
  full)
    ;;
  quick)
    ;;
  *)
    track="quick"
    ;;
esac
if [ "$track" = "full" ]; then
  enforce="${SELFHOST_100MS_FULL_ENFORCE_ON_HOST:-${SELFHOST_100MS_ENFORCE_ON_HOST:-1}}"
  iters="${SELFHOST_100MS_FULL_ITERS:-${SELFHOST_100MS_ITERS:-3}}"
  p95_limit="${SELFHOST_100MS_FULL_P95_MS:-${SELFHOST_100MS_P95_MS:-100}}"
  p99_limit="${SELFHOST_100MS_FULL_P99_MS:-${SELFHOST_100MS_P99_MS:-120}}"
  bootstrap_reuse="${SELFHOST_100MS_FULL_REUSE:-0}"
  bootstrap_timeout="${SELFHOST_100MS_FULL_TIMEOUT:-60}"
  strict_rebuild_default="1"
  stage1_full_rebuild_default="1"
  stage1_require_rebuild_default="1"
else
  enforce="${SELFHOST_100MS_QUICK_ENFORCE_ON_HOST:-${SELFHOST_100MS_ENFORCE_ON_HOST:-0}}"
  iters="${SELFHOST_100MS_QUICK_ITERS:-${SELFHOST_100MS_ITERS:-3}}"
  p95_limit="${SELFHOST_100MS_QUICK_P95_MS:-${SELFHOST_100MS_P95_MS:-30000}}"
  p99_limit="${SELFHOST_100MS_QUICK_P99_MS:-${SELFHOST_100MS_P99_MS:-40000}}"
  bootstrap_reuse="${SELFHOST_100MS_QUICK_REUSE:-1}"
  bootstrap_timeout="${SELFHOST_100MS_QUICK_TIMEOUT:-60}"
  strict_rebuild_default="0"
  stage1_full_rebuild_default="0"
  stage1_require_rebuild_default="0"
fi

if [ "${SELFHOST_100MS_ENFORCE_ON_HOST+x}" != "" ]; then
  enforce="$SELFHOST_100MS_ENFORCE_ON_HOST"
fi
if [ "${SELF_OBJ_BOOTSTRAP_REUSE+x}" != "" ]; then
  bootstrap_reuse="$SELF_OBJ_BOOTSTRAP_REUSE"
fi
if [ "${SELF_OBJ_BOOTSTRAP_TIMEOUT+x}" != "" ]; then
  bootstrap_timeout="$SELF_OBJ_BOOTSTRAP_TIMEOUT"
fi
strict_rebuild="${SELFHOST_STRICT_REBUILD:-$strict_rebuild_default}"
stage1_full_rebuild="${SELFHOST_STAGE1_FULL_REBUILD:-$stage1_full_rebuild_default}"
stage1_require_rebuild="${SELFHOST_STAGE1_REQUIRE_REBUILD:-$stage1_require_rebuild_default}"

case "$iters" in
  ''|*[!0-9]*|0) iters=5 ;;
esac
case "$p95_limit" in
  ''|*[!0-9]*) p95_limit=100 ;;
esac
case "$p99_limit" in
  ''|*[!0-9]*) p99_limit=120 ;;
esac
case "$enforce" in
  0|1) ;;
  *) enforce=1 ;;
esac

out_dir="artifacts/backend_selfhost_100ms_host"
mkdir -p "$out_dir"
report="${SELFHOST_100MS_REPORT:-$out_dir/report.tsv}"
values_file="$out_dir/totals_ms.tsv"
sorted_file="$out_dir/totals_ms.sorted.tsv"
: >"$values_file"

if [ "$host_only" != "" ] && [ "$host_tag" != "$host_only" ]; then
  {
    printf 'host=%s\n' "$host_tag"
    printf 'host_only=%s\n' "$host_only"
    printf 'status=skip_non_target_host\n'
  } >"$report"
  echo "[selfhost_100ms] skip on non-target host: host=$host_tag target=$host_only"
  exit 0
fi

run_id="selfhost100ms.$$"
i=1
while [ "$i" -le "$iters" ]; do
  session="$run_id.$i"
  timing_file="$out_dir/selfhost_timing_${session}.tsv"
  rm -f "$timing_file"
  echo "[selfhost_100ms] run $i/$iters session=$session"
  env \
    SELF_OBJ_BOOTSTRAP_MODE=fast \
    SELF_OBJ_BOOTSTRAP_REUSE="$bootstrap_reuse" \
    SELF_OBJ_BOOTSTRAP_TIMEOUT="$bootstrap_timeout" \
    SELFHOST_STRICT_REBUILD="$strict_rebuild" \
    SELFHOST_STAGE1_FULL_REBUILD="$stage1_full_rebuild" \
    SELFHOST_STAGE1_REQUIRE_REBUILD="$stage1_require_rebuild" \
    SELF_OBJ_BOOTSTRAP_SESSION="$session" \
    SELF_OBJ_BOOTSTRAP_TIMING_OUT="$timing_file" \
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} verify_backend_selfhost_bootstrap_fast >/dev/null
  if [ ! -s "$timing_file" ]; then
    echo "[selfhost_100ms] missing timing file: $timing_file" >&2
    exit 1
  fi
  total_s="$(awk -F '\t' '$1=="total"{print $3; exit}' "$timing_file")"
  if [ "$total_s" = "" ]; then
    echo "[selfhost_100ms] missing total stage in timing file: $timing_file" >&2
    exit 1
  fi
  total_ms="$(awk -v s="$total_s" 'BEGIN{ printf("%.0f", s*1000.0) }')"
  printf '%s\n' "$total_ms" >>"$values_file"
  i=$((i + 1))
done

sort -n "$values_file" >"$sorted_file"

pick_percentile() {
  pct="$1"
  awk -v p="$pct" '
    { a[++n] = $1 }
    END {
      if (n <= 0) {
        print -1
        exit
      }
      k = int((p * n + 99) / 100)
      if (k < 1) k = 1
      if (k > n) k = n
      print a[k]
    }
  ' "$sorted_file"
}

p95_ms="$(pick_percentile 95)"
p99_ms="$(pick_percentile 99)"
max_ms="$(awk 'BEGIN{m=0} { if ($1>m) m=$1 } END{ print m+0 }' "$sorted_file")"

status="ok"
if [ "$p95_ms" -gt "$p95_limit" ] || [ "$p99_ms" -gt "$p99_limit" ]; then
  status="regression"
fi

{
  printf 'host=%s\n' "$host_tag"
  printf 'iters=%s\n' "$iters"
  printf 'p95_ms=%s\n' "$p95_ms"
  printf 'p99_ms=%s\n' "$p99_ms"
  printf 'max_ms=%s\n' "$max_ms"
  printf 'limit_p95_ms=%s\n' "$p95_limit"
  printf 'limit_p99_ms=%s\n' "$p99_limit"
  printf 'status=%s\n' "$status"
} >"$report"

echo "[selfhost_100ms] host=$host_tag p95=${p95_ms}ms p99=${p99_ms}ms max=${max_ms}ms status=$status"

if [ "$status" != "ok" ] && [ "$enforce" = "1" ]; then
  echo "[selfhost_100ms] regression: p95>${p95_limit}ms or p99>${p99_limit}ms" >&2
  exit 1
fi

exit 0
