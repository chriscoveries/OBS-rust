# Dynamic VideoToolbox rate-setting updates

Fork-only review on `88a89d7445aaf87a2445452aba97cf72642753ef`; no upstream-connected branch or public ABI change.

## Unposted issue draft

Title: mac-videotoolbox: changing bitrate limit/window or CRF quality during recording does not update the encoder

Trigger: open a configured VideoToolbox encoder and edit only `max_bitrate`, `max_bitrate_window`, or the CRF quality slider. Keep the previous bitrate and limit-enable state unchanged.

Location: `plugins/mac-videotoolbox/encoder.c`, `vt_update()` around the old `old_bitrate == enc->bitrate && old_limit_bitrate == enc->limit_bitrate` condition.

Expected: a rate-related encoder setting change causes `session_set_bitrate` to apply the new value or report a framework failure. Actual: `vt_update` returns true without any setter call, so the UI settings diverge from the live encode state until restart. The helper also ignores a failed `update_params` return, which is propagated by the production fix.

Severity: medium, ignored user configuration. Restart-only codec/profile changes are deliberately outside this contribution.

## RED then GREEN

- RED: `9bb89991103e30d390503c6af6ac2bf62c31b541`. Adds a self-contained private `vt_rate_state` snapshot and a portable CTest executable. The RED helper reflects the original two-field comparison, producing precisely three failing assertions:
```text
max_bitrate_change_triggers_update: limit ignored
max_window_change_triggers_update: limit duration ignored
quality_change_triggers_update: CRF quality ignored
```
- GREEN: `8f122445c2916159ab57f283d952b019f8e9a643`. Compare all five relevant settings, snapshot before and after `update_params`, and refuse to proceed on its failure. The existing `session_set_bitrate` invocation remains unchanged.
- Final `review:` commit: runner and this handoff only.

## Observed local RED/GREEN

Clang 17 on x86-64 Linux, `-std=c11 -Wall -Wextra -Werror`:

```text
RED -O2: VT rate state: checks=7 failures=3 ; exit 1
GREEN -O2: VT rate state: checks=7 failures=0 ; exit 0
GREEN -O1 ASan/UBSan: VT rate state: checks=7 failures=0 ; exit 0
```

The independent C test exercises the same private helper consumed by `vt_update`. The modified production encoder translation unit itself has **not yet been compiled**, and live VideoToolbox behaviour has not yet been tested. It is not a claim of full OBS native testing.

## Mac native gate

With the source checked out and OFF/ON configured builds:

```sh
uv run test/review/vt-live-rate/run-macos.py OFF_BUILD ON_BUILD
```

This runs the required pinned formatter and Rust lint tools, then builds `libobs`, `mac-videotoolbox` (to compile the actual modified TU) and `test_vt_rate_state`. It discovers the test from CTest, adds the executable, libobs.framework and dependency-library absolute rpaths and ad-hoc signs the test with disabled library validation. It repeats CTest ten times in both modes. No OBS app, upload, timing, new commits or CI dispatch.

## Required authoring environment attempts

```text
clang-format-22 --dry-run --Werror: unavailable (exit 127)
gersemi --check: unavailable (exit 127)
soldr cargo fmt --check: soldr unavailable (exit 127)
soldr cargo clippy --all-targets -- -D warnings: soldr unavailable (exit 127)
```

These gates remain pending. Upstream review also needs platform integration checks showing that changing each value actually calls VideoToolbox. This patch does not assert support for live switching among ABR, CBR and CRF modes; it only handles changes to existing rate settings. Error propagation of DataRateLimits is a separate focused fork draft #90.
