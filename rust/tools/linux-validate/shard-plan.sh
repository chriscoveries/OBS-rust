#!/bin/bash
# Usage: shard-plan.sh COST_FILE NAMES_FILE SHARDS OUT_DIR
# Splits the tests in NAMES_FILE (one per line, from `ctest -N`) into SHARDS
# balanced sets using ctest's own per-test durations (COST_FILE, the
# CTestCostData.txt format: "name runs avg-seconds" lines, then "---" and
# failed tests). Longest test first, each into the lightest shard; a test
# with no recorded cost counts as DEFAULT_COST seconds. Writes
# OUT_DIR/<i>-of-<SHARDS>.regex, an exact-name regex for `ctest -R`.
# Replaces `ctest -I <i>,,<n>` striping, which ignored durations (#1048).
# Tested by ci/test_tier2_shard_plan.py.
set -euo pipefail
[ $# -eq 4 ] || { echo "usage: shard-plan.sh COST_FILE NAMES_FILE SHARDS OUT_DIR" >&2; exit 2; }
cost_file=$1 names_file=$2 shards=$3 out=$4
DEFAULT_COST=${DEFAULT_COST:-1}
mkdir -p "$out"

# name<TAB>cost for every listed test, longest first.
# Match the cost file by name: FNR == NR misreads the names when it is empty.
awk -v def="$DEFAULT_COST" -v cf="$cost_file" '
  FILENAME == cf { if ($0 == "---") done = 1; else if (!done && NF >= 3) cost[$1] = $3; next }
  NF { print $1 "\t" (($1 in cost) ? cost[$1] : def) }
' "$cost_file" "$names_file" | sort -t $'\t' -k2,2gr -k1,1 |
  awk -F '\t' -v n="$shards" -v out="$out" '
    { best = 1
      for (i = 2; i <= n; i++) if (load[i] < load[best]) best = i
      load[best] += $2
      list[best] = (list[best] == "" ? $1 : list[best] "|" $1) }
    END { for (i = 1; i <= n; i++) {
            f = out "/" i "-of-" n ".regex"
            printf "^(%s)$\n", list[i] > f
            printf "shard %d/%d: %.1f s\n", i, n, load[i] } }
  '
