# SIMD logarithm range reduction: owner review

Fork-only refinement of the experimental dynamics work, based on `ef31f677b5f14b109bec6c8b4237f75c592f4256`. It retains the earlier negative-exponent and ratio-1 fixes. Upstream code and the existing review branches remain unchanged. Promotion requires owner approval.

## Cause and fix

`dyn_logf_ps` normalised the significand to [1,2). Its subsequent comparison against sqrt(0.5) could never select the lower branch, and the polynomial was evaluated outside its intended small residual interval. The result has a discontinuity just below powers of two. The fix uses a significand in [0.5,1), with a matching exponent-bias adjustment from 127 to 126, before the existing fold. No new arithmetic approximation or dependency is introduced.

The base header was materialised from connector text and verified against blob `71910538bb7df83443b0a7851df0797e2fa00053` before editing. The regression includes that actual full header and its real audio-math/C99 headers, not a rewritten copy of the logarithm.

## Observed local RED and GREEN

Host: x86-64 Linux, Clang 17. Optimised build uses C11, `-O2 -fno-fast-math -ffp-contract=off -Wall -Wextra -Werror`. Sanitized build uses `-O1 -g -fsanitize=address,undefined` with the same floating-point flags.

The test covers 1,040,392 logarithm inputs (254 positive-normal exponent bins, 4096 sampled mantissas per bin, plus eight boundary/extreme inputs) and five actual compression-gain outputs, including a scalar tail.

| Exact header | Result |
|---|---|
| Original | Exit 1; 366,028 failed assertions, including three gain-output checks |
| Fixed, optimised | Exit 0; zero failures |
| Fixed, ASan/UBSan | Exit 0; zero failures |

Maximum sampled error `abs(actual - log(double(input))) / (1 + abs(log(double(input))))`: original 0.01380526937; fixed 5.884635574e-08. The bound asserted is 1e-6. The gain-output bound is 1e-5 relative to the unchanged scalar audio-math path. These are correctness results, not speed measurements or a proof over every float.

Example at the float immediately below 1.0: original log = +0.01380521059; reference = -5.960464655e-8; fixed = -5.960464478e-8. One affected compression output was 0.3511586487 instead of 0.3548134267.

The new CMake fragment also configured, built and passed CTest in a disposable minimal driver. This checked target registration, not a complete OBS build. Existing filter tests and tolerances remain unchanged.

## Run

```sh
git fetch origin review/dynamics-log-range-20261015
git worktree add ../obsrust-dynamics-log origin/review/dynamics-log-range-20261015
cd ../obsrust-dynamics-log
bash test/review/run-dynamics-log.sh
```

On Apple Silicon with Rosetta already installed, use:

```sh
arch -x86_64 /bin/bash test/review/run-dynamics-log.sh
```

The script selects x86-64 compilation on macOS and runs both optimised and sanitized binaries. It records HEAD/compiler/results. It never builds OBS, pushes, or dispatches CI. A native arm64 invocation exits 77 rather than claiming SSE2 coverage.

For RED, check out test-only commit `d507ab802d4749fe2d6fd91c87e6cc3cba603700` in a separate worktree and compile the new C test with the command/flags above, or run its CTest target. Expect a numeric assertion failure, not a build failure.

## Remaining scope

The normal OBS OFF/ON suite, project formatting/lint and native Mac/Windows runs remain pending. The experimental branch still needs broader exceptional-input and whole-filter state parity plus accepted numerical tolerances and real benchmarks. The helper's supported domain is positive normal inputs; current callers clamp finite small values to at least 1e-30f. This patch does not claim full NaN/subnormal/infinity semantics or make the whole SIMD branch ready to merge.
