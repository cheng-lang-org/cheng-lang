#!/usr/bin/env sh
set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  echo "usage: $0 BASELINE_TXT CANDIDATE_TXT [MAX_RATIO]" >&2
  exit 1
fi

baseline="$1"
candidate="$2"
max_ratio="${3:-}"

if [ ! -f "$baseline" ]; then
  echo "v3 compare: baseline file not found: $baseline" >&2
  exit 1
fi
if [ ! -f "$candidate" ]; then
  echo "v3 compare: candidate file not found: $candidate" >&2
  exit 1
fi

awk -v base_file="$baseline" -v cand_file="$candidate" -v max_ratio="$max_ratio" '
function load(file, map_name,    line, parts, name, value) {
  while ((getline line < file) > 0) {
    if (line ~ /^sink=/) {
      continue
    }
    if (line ~ /^[[:space:]]*$/) {
      continue
    }
    split(line, parts, /[[:space:]]+/)
    name = parts[1]
    if (match(line, /ns_per_op=[0-9.]+/)) {
      value = substr(line, RSTART + 10, RLENGTH - 10) + 0.0
      map_name[name] = value
      order[++order_len] = name
    }
  }
  close(file)
}
BEGIN {
  load(base_file, base_map)
  order_len = 0
  load(cand_file, cand_map)
  printf "%-18s %-12s %-12s %-10s\n", "bench", "base_ns", "cand_ns", "ratio"
  fail = 0
  for (name in base_map) {
    if (!(name in cand_map)) {
      printf "%-18s %-12.2f %-12s %-10s\n", name, base_map[name], "missing", "NA"
      fail = 1
      continue
    }
    ratio = cand_map[name] / base_map[name]
    printf "%-18s %-12.2f %-12.2f %-10.4f\n", name, base_map[name], cand_map[name], ratio
    if (max_ratio != "" && ratio > (max_ratio + 0.0)) {
      fail = 1
    }
  }
  for (name in cand_map) {
    if (!(name in base_map)) {
      printf "%-18s %-12s %-12.2f %-10s\n", name, "missing", cand_map[name], "NA"
      fail = 1
    }
  }
  exit fail
}
' /dev/null
