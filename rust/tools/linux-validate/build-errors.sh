#!/bin/bash
# Usage: build-errors.sh BUILD_LOG
# Prints why a Tier 2 build failed: C/C++ compiler errors and ninja FAILED
# lines (deduplicated), then rustc errors with their source location and
# build-script panics. cargo's messages ("error[E0308]: ...", "error: ...",
# "panicked at ...") don't match the C filter, so a failed Rust build showed
# only "FAILED: [code=101] ...". Tested by ci/test_tier2_build_errors.py.
set -euo pipefail
[ $# -eq 1 ] || { echo "usage: build-errors.sh BUILD_LOG" >&2; exit 2; }
log=$1

grep -E ": (fatal )?error|FAILED:" "$log" | sort -u | head -80 || true

# rustc: an error line, then its "-->" location and code excerpt. Build
# scripts: the panic line and the message under it. A block ends at a blank
# line or at the next line of ordinary build output.
awk '
  /^error(\[E[0-9]+\])?: / || /panicked at / { show = 1; n = 0; print; next }
  show && (/^$/ || /^FAILED:/ || /^ *(Compiling|Checking|Finished) / || /^warning: / || /^\[[0-9]+\/[0-9]+\] /) { show = 0 }
  show { print; if (++n >= 12) show = 0 }
' "$log" | head -200 || true
