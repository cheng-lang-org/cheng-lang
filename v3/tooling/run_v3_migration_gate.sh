#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
default_compiler="$root/artifacts/v3_backend_driver/cheng"
compiler="${1:-$default_compiler}"
label="${2:-host}"
current_src="$root/v3/src/tests/compiler_migration_fixture.cheng"
legacy_src="$root/v3/testdata/compiler_migration/compiler_migration_fixture_legacy.cheng"
out_dir="$root/artifacts/v3_migration_gate/$label"
migrate_prefix="$out_dir/compiler_migration_fixture.migrate"
proof_prefix="$out_dir/compiler_migration_fixture.prove"
blocked_prefix="$out_dir/compiler_migration_fixture.blocked"
publish_prefix="$out_dir/compiler_migration_fixture.publish"
migrate_log="$out_dir/migrate_csg.log"
prove_log="$out_dir/prove_migration.log"
blocked_log="$out_dir/publish_blocked.log"
publish_log="$out_dir/publish_migration.log"

mkdir -p "$out_dir"
cd "$root"

if [ "$compiler" = "$default_compiler" ] && [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$compiler" ]; then
  echo "v3 migration gate: missing compiler: $compiler" >&2
  exit 1
fi

if [ ! -f "$current_src" ] || [ ! -f "$legacy_src" ]; then
  echo "v3 migration gate: missing fixture sources" >&2
  exit 1
fi

echo "[v3 migration gate] migrate-csg $label"
"$compiler" migrate-csg \
  "--root:$root/v3" \
  "--in:$current_src" \
  "--legacy-in:$legacy_src" \
  "--out:$migrate_prefix" >"$migrate_log" 2>&1
cat "$migrate_log"

for suffix in .migration.txt .migration.csg.txt .migration.surface.txt .migration.receipt.txt
do
  if [ ! -f "$migrate_prefix$suffix" ]; then
    echo "v3 migration gate: missing migrate artifact: $migrate_prefix$suffix" >&2
    exit 1
  fi
done

grep -q '^canonical_graph_cid=' "$migrate_log"

echo "[v3 migration gate] prove-migration $label"
"$compiler" prove-migration \
  "--root:$root/v3" \
  "--in:$current_src" \
  "--legacy-in:$legacy_src" \
  "--target:arm64-apple-darwin" \
  "--channel:stable" \
  "--out:$proof_prefix" >"$prove_log" 2>&1
cat "$prove_log"

for suffix in .migration.txt .migration.csg.txt .migration.surface.txt .migration.receipt.txt .proof.txt
do
  if [ ! -f "$proof_prefix$suffix" ]; then
    echo "v3 migration gate: missing prove artifact: $proof_prefix$suffix" >&2
    exit 1
  fi
done

grep -Fxq 'equivalence_kind=canonical_semantic_identity' "$prove_log"
grep -Fxq 'graph_equivalent=1' "$prove_log"
grep -Fxq 'export_surface_compatible=1' "$prove_log"
grep -Fxq 'compile_receipt_equivalent=1' "$prove_log"

echo "[v3 migration gate] publish-world stable reject without proof $label"
blocked_rc=0
set +e
"$compiler" publish-world \
  "--root:$root/v3" \
  "--in:$current_src" \
  "--target:arm64-apple-darwin" \
  "--out:$blocked_prefix" \
  "--channel:stable" >"$blocked_log" 2>&1
blocked_rc="$?"
set -e
cat "$blocked_log"

if [ "$blocked_rc" -eq 0 ]; then
  echo "v3 migration gate: stable publish unexpectedly passed without proof" >&2
  exit 1
fi

grep -Eq 'stable requires|publish blocked|missing --baseline' "$blocked_log"

echo "[v3 migration gate] publish-world migration $label"
"$compiler" publish-world \
  "--root:$root/v3" \
  "--in:$current_src" \
  "--legacy-in:$legacy_src" \
  "--target:arm64-apple-darwin" \
  "--out:$publish_prefix" \
  "--channel:stable" >"$publish_log" 2>&1
cat "$publish_log"

for suffix in .world.txt .compiler.snapshot.txt .std.snapshot.txt .runtime.snapshot.txt .csg.txt .surface.txt .receipt.txt .lock.toml .migration.txt .migration.csg.txt .migration.surface.txt .migration.receipt.txt .proof.txt .publish.txt
do
  if [ ! -f "$publish_prefix$suffix" ]; then
    echo "v3 migration gate: missing publish artifact: $publish_prefix$suffix" >&2
    exit 1
  fi
done

grep -Fxq 'proof_mode=migration' "$publish_log"
grep -Fxq 'equivalence_passed=1' "$publish_log"
grep -Fxq 'publish_allowed=1' "$publish_log"

echo "v3 migration gate ok ($label)"
