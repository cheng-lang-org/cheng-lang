#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

usage() {
  cat <<'EOF'
Usage:
  src/tooling/rawptr_migrate_ffi.sh [--file:<path> ...] [--root:<dir> ...]
                                   [--apply] [--check]
                                   [--report:<path>] [--suggestions:<path>]
                                   [--backup-manifest:<path>]
  src/tooling/rawptr_migrate_ffi.sh --rollback:<manifest>

Notes:
  - Rewrites legacy `openArray[T]` surface to `T[]`.
  - Reports user-surface raw pointer risks (`void*`, pointer types, pointer ops).
  - `--check` exits non-zero when migration or raw-pointer risks remain.
  - `--rollback` restores files recorded in the backup manifest.
EOF
}

fail() {
  echo "[rawptr_migrate_ffi] $1" 1>&2
  exit 1
}

ensure_parent_dir() {
  target="$1"
  parent="$(dirname -- "$target")"
  if [ "$parent" != "." ] && [ "$parent" != "" ]; then
    mkdir -p "$parent"
  fi
}

mktemp_in_root() {
  template="$1"
  mktemp "$root/chengcache/$template" 2>/dev/null || printf '%s\n' "$root/chengcache/$template.$$"
}

count_openarray_hits() {
  perl -0ne '$c += () = /\bopenArray\s*\[[^][]+\]/g; END { print $c }' "$1"
}

count_voidptr_hits() {
  perl -0ne '$c += () = /\bvoid\s*\*/g; END { print $c }' "$1"
}

count_pointer_type_hits() {
  perl -0ne '$c += () = /\b[A-Za-z_][A-Za-z0-9_]*\s*\*/g; END { print $c }' "$1"
}

count_pointer_ops_hits() {
  perl -0ne '$c += () = /\bptr_(?:add|sub|diff|load|store|cast|eq|ne)\b|\bvoid\s*\*\s*\(/g; END { print $c }' "$1"
}

rewrite_openarray_to_slice() {
  perl -0pe 's/\bopenArray\s*\[\s*([^\[\]]+?)\s*\]/$1\[\]/g' "$1"
}

rollback_manifest=""
apply_mode=0
check_mode=0
report_path=""
suggestions_path=""
backup_manifest=""
files_tmp="$(mktemp_in_root ".rawptr_migrate_ffi_files.XXXXXX")"
roots_tmp="$(mktemp_in_root ".rawptr_migrate_ffi_roots.XXXXXX")"
cleanup() {
  rm -f "$files_tmp" "$roots_tmp"
}
trap cleanup EXIT
: >"$files_tmp"
: >"$roots_tmp"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --file:*)
      printf '%s\n' "${1#--file:}" >>"$files_tmp"
      ;;
    --root:*)
      printf '%s\n' "${1#--root:}" >>"$roots_tmp"
      ;;
    --apply)
      apply_mode=1
      ;;
    --check)
      check_mode=1
      ;;
    --report:*)
      report_path="${1#--report:}"
      ;;
    --suggestions:*)
      suggestions_path="${1#--suggestions:}"
      ;;
    --backup-manifest:*)
      backup_manifest="${1#--backup-manifest:}"
      ;;
    --rollback:*)
      rollback_manifest="${1#--rollback:}"
      ;;
    *)
      echo "[rawptr_migrate_ffi] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

if [ "$rollback_manifest" != "" ]; then
  if [ ! -f "$rollback_manifest" ]; then
    fail "missing rollback manifest: $rollback_manifest"
  fi
  rollback_count=0
  while IFS="$(printf '\t')" read -r original backup _rest; do
    [ "$original" = "" ] && continue
    [ "$backup" = "" ] && continue
    if [ ! -f "$backup" ]; then
      fail "missing backup recorded in manifest: $backup"
    fi
    ensure_parent_dir "$original"
    cp "$backup" "$original"
    rollback_count=$((rollback_count + 1))
  done <"$rollback_manifest"
  echo "status=rolled_back"
  echo "restored_files=$rollback_count"
  echo "manifest=$rollback_manifest"
  exit 0
fi

if [ ! -s "$files_tmp" ] && [ ! -s "$roots_tmp" ]; then
  echo "[rawptr_migrate_ffi] missing --file:<path> or --root:<dir>" 1>&2
  usage 1>&2
  exit 2
fi

all_targets_tmp="$(mktemp_in_root ".rawptr_migrate_ffi_targets.XXXXXX")"
trap 'cleanup; rm -f "$all_targets_tmp"' EXIT
: >"$all_targets_tmp"

while IFS= read -r path; do
  [ "$path" = "" ] && continue
  printf '%s\n' "$path" >>"$all_targets_tmp"
done <"$files_tmp"

while IFS= read -r scan_root; do
  [ "$scan_root" = "" ] && continue
  if [ -f "$scan_root" ]; then
    printf '%s\n' "$scan_root" >>"$all_targets_tmp"
    continue
  fi
  if [ ! -d "$scan_root" ]; then
    fail "scan root not found: $scan_root"
  fi
  if ! command -v rg >/dev/null 2>&1; then
    fail "rg is required for --root scans"
  fi
  rg --files "$scan_root" -g '*.cheng' >>"$all_targets_tmp"
done <"$roots_tmp"

dedup_targets_tmp="$(mktemp_in_root ".rawptr_migrate_ffi_targets_dedup.XXXXXX")"
trap 'cleanup; rm -f "$all_targets_tmp" "$dedup_targets_tmp"' EXIT
awk 'NF > 0 && !seen[$0]++ { print }' "$all_targets_tmp" >"$dedup_targets_tmp"
if [ ! -s "$dedup_targets_tmp" ]; then
  fail "no target files found"
fi

if [ "$report_path" != "" ]; then
  ensure_parent_dir "$report_path"
fi
if [ "$suggestions_path" != "" ]; then
  ensure_parent_dir "$suggestions_path"
  : >"$suggestions_path"
fi
if [ "$backup_manifest" != "" ]; then
  ensure_parent_dir "$backup_manifest"
  : >"$backup_manifest"
  backup_dir="$(dirname -- "$backup_manifest")/rawptr_migrate_ffi.backups"
  mkdir -p "$backup_dir"
else
  backup_dir=""
fi

target_count=0
applied_file_count=0
legacy_openarray_hits=0
risk_voidptr_hits=0
risk_pointer_type_hits=0
risk_pointer_ops_hits=0
backup_index=0

while IFS= read -r target; do
  [ "$target" = "" ] && continue
  if [ ! -f "$target" ]; then
    fail "missing target file: $target"
  fi
  target_count=$((target_count + 1))
  open_hits="$(count_openarray_hits "$target")"
  void_hits="$(count_voidptr_hits "$target")"
  ptr_type_hits="$(count_pointer_type_hits "$target")"
  ptr_ops_hits="$(count_pointer_ops_hits "$target")"

  legacy_openarray_hits=$((legacy_openarray_hits + open_hits))
  risk_voidptr_hits=$((risk_voidptr_hits + void_hits))
  risk_pointer_type_hits=$((risk_pointer_type_hits + ptr_type_hits))
  risk_pointer_ops_hits=$((risk_pointer_ops_hits + ptr_ops_hits))

  if [ "$suggestions_path" != "" ]; then
    if [ "$open_hits" -gt 0 ]; then
      printf '%s\treplace openArray[T] with T[]\n' "$target" >>"$suggestions_path"
    fi
    if [ "$void_hits" -gt 0 ] || [ "$ptr_type_hits" -gt 0 ] || [ "$ptr_ops_hits" -gt 0 ]; then
      printf '%s\treplace raw pointer surface with slice/handle/borrow bridge\n' "$target" >>"$suggestions_path"
    fi
  fi

  if [ "$apply_mode" = "1" ] && [ "$open_hits" -gt 0 ]; then
    rewritten_tmp="$(mktemp_in_root ".rawptr_migrate_ffi_apply.XXXXXX")"
    rewrite_openarray_to_slice "$target" >"$rewritten_tmp"
    if ! cmp -s "$target" "$rewritten_tmp"; then
      if [ "$backup_manifest" != "" ]; then
        backup_index=$((backup_index + 1))
        backup_path="$backup_dir/$(basename -- "$target").$backup_index.bak"
        cp "$target" "$backup_path"
        printf '%s\t%s\n' "$target" "$backup_path" >>"$backup_manifest"
      fi
      cp "$rewritten_tmp" "$target"
      applied_file_count=$((applied_file_count + 1))
    fi
    rm -f "$rewritten_tmp"
  fi
done <"$dedup_targets_tmp"

status="ok"
if [ "$legacy_openarray_hits" -gt 0 ] || [ "$risk_voidptr_hits" -gt 0 ] || \
   [ "$risk_pointer_type_hits" -gt 0 ] || [ "$risk_pointer_ops_hits" -gt 0 ]; then
  status="needs_migration"
fi

if [ "$apply_mode" = "1" ]; then
  legacy_openarray_hits=0
  risk_voidptr_hits=0
  risk_pointer_type_hits=0
  risk_pointer_ops_hits=0
  while IFS= read -r target; do
    [ "$target" = "" ] && continue
    legacy_openarray_hits=$((legacy_openarray_hits + $(count_openarray_hits "$target")))
    risk_voidptr_hits=$((risk_voidptr_hits + $(count_voidptr_hits "$target")))
    risk_pointer_type_hits=$((risk_pointer_type_hits + $(count_pointer_type_hits "$target")))
    risk_pointer_ops_hits=$((risk_pointer_ops_hits + $(count_pointer_ops_hits "$target")))
  done <"$dedup_targets_tmp"
  status="ok"
  if [ "$legacy_openarray_hits" -gt 0 ] || [ "$risk_voidptr_hits" -gt 0 ] || \
     [ "$risk_pointer_type_hits" -gt 0 ] || [ "$risk_pointer_ops_hits" -gt 0 ]; then
    status="needs_migration"
  fi
fi

report_tmp="$(mktemp_in_root ".rawptr_migrate_ffi_report.XXXXXX")"
{
  echo "status=$status"
  echo "target_count=$target_count"
  echo "applied_file_count=$applied_file_count"
  echo "legacy_openarray_hits=$legacy_openarray_hits"
  echo "risk_voidptr_hits=$risk_voidptr_hits"
  echo "risk_pointer_type_hits=$risk_pointer_type_hits"
  echo "risk_pointer_ops_hits=$risk_pointer_ops_hits"
  if [ "$backup_manifest" != "" ]; then
    echo "backup_manifest=$backup_manifest"
  fi
  if [ "$suggestions_path" != "" ]; then
    echo "suggestions=$suggestions_path"
  fi
} >"$report_tmp"

if [ "$report_path" != "" ]; then
  cp "$report_tmp" "$report_path"
else
  cat "$report_tmp"
fi
rm -f "$report_tmp"

if [ "$check_mode" = "1" ] && [ "$status" != "ok" ]; then
  exit 1
fi
exit 0
