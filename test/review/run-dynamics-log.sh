#!/usr/bin/env bash
# CPU-only regression of the production header. No OBS build, capture or device needed.
set -euo pipefail
cd "$(dirname "$0")/../.."
case "$(uname -m)" in
  x86_64|amd64) ;;
  *) echo 'Requires x86-64 (use an x86-64 shell/compiler under Rosetta on Apple Silicon).' >&2; exit 77 ;;
esac
cc="${CC:-clang}"
command -v "$cc" >/dev/null || { echo "Missing compiler: $cc" >&2; exit 2; }
logs="$(mktemp -d "${TMPDIR:-/tmp}/obs-dynamics-log.XXXXXX")"
echo "Logs: $logs"
git rev-parse HEAD > "$logs/head.txt"
"$cc" --version > "$logs/compiler.txt"
flags=(-std=c11 -fno-fast-math -ffp-contract=off -Wall -Wextra -Werror -Ilibobs)
if [[ "$(uname -s)" == Darwin ]]; then
  flags+=(-arch x86_64)
fi
"$cc" "${flags[@]}" -O2 test/cmocka/test_dynamics_log_range.c -lm -o "$logs/optimized"
"$logs/optimized" 2>&1 | tee "$logs/optimized.log"
"$cc" "${flags[@]}" -O1 -g -fsanitize=address,undefined test/cmocka/test_dynamics_log_range.c -lm -o "$logs/sanitized"
"$logs/sanitized" 2>&1 | tee "$logs/sanitized.log"
