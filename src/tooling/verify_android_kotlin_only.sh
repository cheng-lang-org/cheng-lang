#!/usr/bin/env sh
set -eu

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_android_kotlin_only.sh --project:<android_project_dir>
EOF
}

project=""
while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --project:*)
      project="${1#--project:}"
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

if [ "$project" = "" ]; then
  echo "[Error] missing --project:<android_project_dir>" 1>&2
  exit 2
fi
if [ ! -d "$project" ]; then
  echo "[Error] Android project not found: $project" 1>&2
  exit 2
fi

main_dir="$project/app/src/main"
kotlin_dir="$main_dir/kotlin"
if [ ! -d "$kotlin_dir" ]; then
  echo "[Error] Kotlin source directory not found: $kotlin_dir" 1>&2
  exit 2
fi

java_file="$(find "$main_dir" -type f -name '*.java' | head -n 1 || true)"
if [ "$java_file" != "" ]; then
  echo "[Error] Java source is not allowed for Android host: $java_file" 1>&2
  echo "[Error] Android integration is Kotlin-only; move host sources to app/src/main/kotlin." 1>&2
  exit 2
fi

echo "[verify-android-kotlin-only] ok project=$project"
