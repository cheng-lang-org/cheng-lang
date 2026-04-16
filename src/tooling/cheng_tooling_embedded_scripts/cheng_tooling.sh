#!/usr/bin/env sh
:
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"

echo "[cheng_tooling] retired: src/tooling/cheng_tooling.cheng 已移除" 1>&2
echo "[cheng_tooling] v3 编译和 v3 gate 统一入口: $root/v3/tooling/cheng_v3.sh" 1>&2
exit 2
