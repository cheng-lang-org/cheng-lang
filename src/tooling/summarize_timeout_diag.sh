#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/summarize_timeout_diag.sh [--file:<sample.txt> | --dir:<diag_dir>] [--latest:<n>] [--top:<n>] [--include-unknown]

Notes:
  - Summarizes macOS `sample` call-graph files and prints top hot frames.
  - Default search dirs:
      chengcache/backend_timeout_diag
      chengcache/libp2p_frontier/logs/timeout_diag
  - By default, `???` frames are filtered out; use `--include-unknown` to keep them.
EOF
}

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

single_file=""
diag_dir=""
latest="1"
top_n="12"
include_unknown="0"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --file:*)
      single_file="${1#--file:}"
      ;;
    --dir:*)
      diag_dir="${1#--dir:}"
      ;;
    --latest:*)
      latest="${1#--latest:}"
      ;;
    --top:*)
      top_n="${1#--top:}"
      ;;
    --include-unknown)
      include_unknown="1"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[timeout_diag] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

case "$latest" in
  ''|*[!0-9]*)
    echo "[timeout_diag] invalid --latest: $latest" 1>&2
    exit 2
    ;;
esac
case "$top_n" in
  ''|*[!0-9]*)
    echo "[timeout_diag] invalid --top: $top_n" 1>&2
    exit 2
    ;;
esac
if [ "$latest" -lt 1 ]; then
  latest="1"
fi
if [ "$top_n" -lt 1 ]; then
  top_n="1"
fi

collect_files() {
  if [ "$single_file" != "" ]; then
    if [ ! -f "$single_file" ]; then
      echo "[timeout_diag] missing file: $single_file" 1>&2
      exit 2
    fi
    printf '%s\n' "$single_file"
    return
  fi

  if [ "$diag_dir" != "" ]; then
    ls -1t "$diag_dir"/*.sample.txt 2>/dev/null | head -n "$latest" || true
    return
  fi

  {
    ls -1t chengcache/backend_timeout_diag/*.sample.txt 2>/dev/null || true
    ls -1t chengcache/libp2p_frontier/logs/timeout_diag/*.sample.txt 2>/dev/null || true
  } | awk '!seen[$0]++' | head -n "$latest"
}

extract_counts() {
  keep_unknown="$1"
  sample_file="$2"
  awk -v keep_unknown="$keep_unknown" '
    {
      line = $0;
      gsub(/^[[:space:]+!|:]*/, "", line);
      if (line !~ /^[0-9]+[[:space:]]+/) next;

      cnt = line;
      sub(/[[:space:]].*$/, "", cnt);
      line = substr(line, length(cnt) + 1);
      sub(/^[[:space:]]+/, "", line);

      frame = line;
      sub(/[[:space:]]+\(in .*/, "", frame);
      sub(/[[:space:]]+$/, "", frame);
      if (frame == "") next;
      if (frame ~ /^Thread_/) next;
      if (frame == "start") next;
      if (keep_unknown != 1 && frame == "???") next;

      print cnt "\t" frame;
    }
  ' "$sample_file"
}

mkdir -p "$root/chengcache"
tmp_files="$(mktemp "$root/chengcache/.timeout_diag_files.XXXXXX" 2>/dev/null || echo "$root/chengcache/.timeout_diag_files.$$")"
tmp_counts="$(mktemp "$root/chengcache/.timeout_diag_counts.XXXXXX" 2>/dev/null || echo "$root/chengcache/.timeout_diag_counts.$$")"
cleanup_tmp() {
  rm -f "$tmp_files" "$tmp_counts"
}
trap cleanup_tmp EXIT

collect_files >"$tmp_files"
if [ ! -s "$tmp_files" ]; then
  echo "[timeout_diag] no sample files found" 1>&2
  exit 2
fi

file_count="$(wc -l <"$tmp_files" | tr -d ' ')"
echo "== timeout_diag.summary =="
echo "files=$file_count"
while IFS= read -r f; do
  [ "$f" = "" ] && continue
  echo "file=$f"
done <"$tmp_files"

: >"$tmp_counts"
while IFS= read -r sample_file; do
  [ "$sample_file" = "" ] && continue
  extract_counts "$include_unknown" "$sample_file" >>"$tmp_counts" 2>/dev/null || true
done <"$tmp_files"
if [ ! -s "$tmp_counts" ] && [ "$include_unknown" = "0" ]; then
  : >"$tmp_counts"
  while IFS= read -r sample_file; do
    [ "$sample_file" = "" ] && continue
    extract_counts 1 "$sample_file" >>"$tmp_counts" 2>/dev/null || true
  done <"$tmp_files"
fi

if [ ! -s "$tmp_counts" ]; then
  echo "top=none"
  exit 0
fi

echo "top=$top_n"
awk -F '\t' '{sum[$2]+=$1} END{for (k in sum) printf "%d\t%s\n", sum[k], k}' "$tmp_counts" \
  | sort -nr -k1,1 \
  | head -n "$top_n" \
  | while IFS="$(printf '\t')" read -r cnt frame; do
      [ "$frame" = "" ] && continue
      echo "$cnt	$frame"
    done
