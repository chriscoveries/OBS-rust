#!/bin/bash
# Usage: merge-costs.sh COST_FILE... > merged
# Merges the CTestCostData.txt files ("name runs avg-seconds" lines, then
# "---" and failed tests) that the Tier 2 shards of one mode wrote. Every
# shard starts from the same data and bumps the run count only of the tests
# it ran, so each test takes the line with the highest run count. Failed
# tests are the union. Tested by ci/test_tier2_cost_freshness.py (#930).
set -euo pipefail
awk '
  FNR == 1 { failed_part = 0 }
  $0 == "---" { failed_part = 1; next }
  failed_part { if (NF && !($1 in failed)) { failed[$1] = 1; order[++nf] = $1 }; next }
  NF >= 3 { if (!($1 in runs) || $2 + 0 > runs[$1]) { runs[$1] = $2 + 0; line[$1] = $0 } }
  END {
    for (n in line) print line[n] | "sort"
    close("sort")
    print "---"
    for (i = 1; i <= nf; i++) print order[i]
  }
' "$@"
