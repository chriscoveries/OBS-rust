# Scaler alias fix: Mac validation handoff

Owner-review branch only. The original upstream-connected branch is unchanged.
This branch is a patch on `40e16803789a68051f4f66cbf05556f665ff6798`.
Do not merge it into its base, push to an upstream-connected branch, post an
upstream comment, or mark an upstream PR ready without the owner's approval.

## Implemented

The direct-output predicate now builds checked spans for every source and
destination plane. It falls back to scratch-copy on any input/output overlap,
including cross-plane aliases, and on overlapping destination planes. Source
chroma heights round up. Address wrap, size overflow, null/empty spans and
strides that would become negative when passed to FFmpeg are excluded from
the fast path. The fallback and public API are unchanged.

Two new test executables are registered with CTest. Existing C tests are
unchanged. `test_video_scaler_alias` covers BGRA, I420, NV12, P010 and YUVA
sources scaled to BGRA, with exact aliasing, offsets in either direction and
output beginning in a source chroma plane. It checks three distinct frames
per layout, all visible output bytes, and unchanged storage outside output.
`test_video_scaler_spans` exercises the same private predicates as production,
including all 16 source/destination plane pairings, destination-plane aliasing,
adjacent disjoint spans and arithmetic limits.

## Run on the Mac

Use a fresh worktree so the owner's active checkout stays untouched:

```sh
git fetch https://github.com/chriscoveries/obsrust-fork.git review/scaler-alias-20261015
git worktree add --detach ../obsrust-scaler-review FETCH_HEAD
cd ../obsrust-scaler-review
git submodule update --init --recursive
bash test/review/run-scaler-alias-macos.sh
```

The runner configures separate OFF/ON builds and builds only libobs plus the
scaler test targets. It repeats the scaler tests ten times, configures dyld
paths, and records configure/build/test logs in `build_review_scaler/`.
It assumes the existing project Rust setup, Xcode, CMake and Homebrew cmocka.
It does not install tools, alter Git state or send network test traffic.
CMake may acquire the preset's normal OBS build dependencies.

For full validation afterward:

```sh
./lint
bash rust/tools/tier2-validate/validate.sh --repeat 10 --tests '^test_video_scaler(_alias|_spans)?$'
```

The official harness also checks the full C suite and OFF/ON export parity.

## Demonstrate the regression

The test-only checkpoint is `64b2e58da8fe36044f2df2f3997c3819c763f3ad`.
From the fixed worktree, use its runner against a separate RED worktree:

```sh
git worktree add --detach ../obsrust-scaler-red 64b2e58da8fe36044f2df2f3997c3819c763f3ad
SCALER_REVIEW_SOURCE_DIR="$(cd ../obsrust-scaler-red && pwd -P)" \
  SCALER_REVIEW_MODES=OFF bash test/review/run-scaler-alias-macos.sh
# Repeat with SCALER_REVIEW_MODES=ON.
```

Expected RED: an output-memory assertion in `scaler_alias_bgra` or
`scaler_alias_planar`. A configure/link failure or crash is not accepted as
proof. Expected fixed result: all three scaler executables pass in OFF/ON.
Record both commit IDs and the exact failing/passing test output.

## Evidence already obtained here

The byte-identical base source was checked against Git blob
`f04b89aafb7d6247cd7244b5bafb7628d339798e` before editing.
The standalone production predicates compiled with Clang C11,
`-Wall -Wextra -Werror -fsanitize=address,undefined`; all 65,536 exhaustive
small-range comparisons plus plane/overflow checks passed. The runner passes
`bash -n`. A prior CPU-only libswscale probe demonstrates the alias corruption.

The full OBS build and this new cmocka executable have not run here. Mac
execution, repository formatting/lint and full export parity remain pending.
No speed-up is claimed. This patch repairs correctness; benchmarking the
original direct-scaling optimization remains separate.
