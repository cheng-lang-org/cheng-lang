#!/usr/bin/env sh
# Map unprefixed env vars <-> CHENG_* env vars for tooling scripts.
# New convention: unprefixed names are canonical; CHENG_* remains compatibility.

if [ "${ENV_PREFIX_BRIDGE_LOADED_PID:-}" = "$$" ]; then
  return 0 2>/dev/null || exit 0
fi
ENV_PREFIX_BRIDGE_LOADED_PID="$$"
export ENV_PREFIX_BRIDGE_LOADED_PID

bridge_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
bridge_list="$bridge_dir/env_prefix_vars.list"

if [ ! -f "$bridge_list" ]; then
  return 0 2>/dev/null || exit 0
fi

while IFS= read -r pref; do
  case "$pref" in
    ''|'#'*)
      continue
      ;;
  esac

  case "$pref" in
    CHENG_[A-Z0-9_]*)
      bare="${pref#CHENG_}"
      ;;
    [A-Z0-9_]*)
      bare="$pref"
      pref="CHENG_$bare"
      ;;
    *)
      continue
      ;;
  esac

  eval "bare_set=\${$bare+x}"
  eval "pref_set=\${$pref+x}"

  if [ "${bare_set:-}" = "x" ]; then
    eval "export $pref=\${$bare}"
  elif [ "${pref_set:-}" = "x" ]; then
    eval "export $bare=\${$pref}"
  fi
done < "$bridge_list"

# `emit=obj` has been retired; bridge no longer injects legacy allow flags.
