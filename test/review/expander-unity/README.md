# Expander unity-ratio review

Fork-only draft on `087a25c7d977ad3fbcc12a572d95ec05042824de`. Production diff: one calculation and its comment. No workflow, public API or upstream-connected branch changes.

## Issue draft

Title: obs-filters: unity-ratio expander attenuates audio after silence

Trigger: set Expander ratio to 1:1, feed digital silence, then resume audio.
Location at the base: `plugins/obs-filters/expander-filter.c:353`, in `process_sample`, reached from `expander_filter_audio`.
Expected: neutral gain stays neutral; samples retain the configured output gain. Existing attenuation continues its normal attack/release transition.
Actual: zero slope times an infinite level difference produces NaN, so fmaxf selects the -60 dB floor. Silence accumulates attenuation and resumed audio fades up.
Severity: medium, audible output error in a supported configuration. A scoped search of open/closed upstream expander PRs and reports found no matching scalar-unity fix.

## Commits

RED: `22b975228c528280edb1e95fcb4587450d9bbd2b`.
GREEN: `fd67a46f6832d0b54ba6e26997c8dde5a28e453f`.
This final `review:` commit contains the handoff and runner only.

Original production blob: `077451e74b0c0302ce1cf5c9c1931c3c673d7157`.
Fixed production blob: `c65ca03cd3ad2e534ead3588a02d113e2cd3aaf0`.

## Observed checks

Complete production filter translation unit, compiled with a local OBS dependency facade. The facade supplies allocation and unused property/settings interfaces; it is not part of the patch. This proves the scalar algorithm and callback tests, not native libobs ABI linkage.

Clang 17, C11, `-Wall -Wextra -Werror -fno-fast-math -ffp-contract=off`:

```
RED, -O1 -g -fsanitize=address,undefined:
ratio_one_silence_keeps_zero_gain: silence changed gain at ratio 1:1
ratio_one_silence_then_signal_is_passthrough: silent blocks accumulated attenuation
ratio_one_silence_then_signal_is_passthrough: signal after silence differs from input times output gain
expander unity: checks=360744 failures=360742
exit=1

GREEN, -O2 -DNDEBUG:
expander unity: checks=360744 failures=0
exit=0

GREEN, -O1 -g -fsanitize=address,undefined:
expander unity: checks=360744 failures=0
exit=0
```

The gain-history test expects -6 dB after one attack step from -12 dB; the non-unity and upward-compressor guards are unchanged. Five sample rates, 1/2/6/8 channels, both detector modes, two output gains and consecutive silence/signal blocks are covered. No microphone, GPU or running OBS frontend is needed.

## Native validation

Use configured OFF and ON build directories for this source worktree with unit tests enabled:

```
bash test/review/expander-unity/run-macos.sh OFF_BUILD ON_BUILD
```

Only libobs and test_expander_unity are built. The runner selects the test through CTest, adds absolute executable/framework/dependency rpaths, ad-hoc signs the test with disable-library-validation, and repeats it ten times per mode. It never creates a commit or changes branch refs. Other platforms can build the same two targets and run `ctest -R '^test_expander_unity$' --no-tests=error --output-on-failure` in both configurations.

## Required formatting checks

```
$ clang-format-22 --dry-run --Werror plugins/obs-filters/expander-filter.c test/cmocka/test_expander_unity.c
bash: line 1: clang-format-22: command not found
exit=127
$ gersemi --check test/cmocka/tests.d/test_expander_unity.cmake
bash: line 1: gersemi: command not found
exit=127
$ soldr cargo fmt --check
bash: line 1: soldr: command not found
exit=127
$ soldr cargo clippy --all-targets -- -D warnings
bash: line 1: soldr: command not found
exit=127
git diff --check: exit=0
bash -n on runner: exit=0
```

Pinned formatter retrieval failed on package-host DNS. These remain unpassed gates. No Rust source changed. Native OFF/ON test receipts and formatter checks are required before promotion; no CI dispatch or timing run was performed.
