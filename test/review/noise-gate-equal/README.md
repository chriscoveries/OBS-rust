# Noise gate: equal thresholds must not latch the gate open

Private fork draft based on `087a25c7d977ad3fbcc12a572d95ec05042824de`. No upstream issue, workflow or branch changed.

## Unposted issue draft

Title: bug: obs-filters noise gate remains open indefinitely at equal thresholds

Trigger: Set Noise Gate open and close thresholds to the same value (both sliders permit it), play a signal that opens the gate, then stop the input.

Location: `plugins/obs-filters/noise-gate-filter.c`, `noise_gate_update()` and `noise_gate_filter_audio()`.

Expected: without hysteresis, the level detector follows the current level; when silence arrives, the gate closes, then the ordinary hold/release envelope attenuates it. Actual: `decay_rate = (open - close) / decay_period = 0`, so `level = max(previous_level, current_level)` never falls, `is_open` remains true and attenuation remains 1.0 after arbitrarily long silence.

Severity: medium, user-visible filter malfunction, not memory-safety.

## Source change

For `decay_rate == 0.0f`, update the detector with `cur_level` instead of an indefinitely-held maximum. For every nonzero decay value, leave the old mathematical expression untouched. Audio hold/release logic, defaults and normal hysteresis remain unchanged. Inverted thresholds are not otherwise normalized; their semantics remain a separate follow-up.

## Test-first sequence

- RED: `141807040c9c445229d47b44c29a833e21690bfa`. New `test_noise_gate_equal_threshold` includes the actual production filter callback; no mock DSP path. Twenty combinations of five sampling rates and 1/2/6/8 channels exercise a 128-sample burst followed by 64 silent 1,024-sample blocks. Additionally checks exact audio output and carried state against the unchanged scalar formula on positive-decay ordinary hysteresis.
- GREEN: `2050547dd0a69efc72c82accd1ebafd8893d25ad`.
- Final `review:` commit: runner and handoff only.

Expected RED assertions in the native test (unexecuted here):

```text
equal_threshold_closes_after_silence: gate remains open
equal_threshold_releases_after_silence: output remains unattenuated
```

## Observed local independent reproduction

A standalone C program copied the scalar production state update; it exercised the same twenty equal-threshold sample-rate/channel combinations using ordinary floating-point maths:

```text
RED, Clang 17 -O2: 20/20 cases stuck open; failures=20; exit=1
GREEN, Clang 17 -O2 -DNDEBUG: failures=0; exit=0
GREEN, Clang 17 -O1 -g -fsanitize=address,undefined: failures=0; exit=0
```

This is an isolated DSP reproduction, **not the native test executable**. The registered native CTest must be built and run OFF/ON with real OBS headers. It includes production filter code; libobs supplies ordinary symbols. No microphone or GUI is needed.

## Native Mac runner

In clean source checkout with two configured CMake builds:

```sh
bash test/review/noise-gate-equal/run-macos.sh OFF_BUILD ON_BUILD
```

Runs formatter/lint first, builds libobs and only the targeted CTest executable, validates the build directory and CTest executable path, adds absolute executable/framework/dependency rpaths and ad-hoc signs the focused test to allow Homebrew dependencies. Repeats ten times per configuration. No runtime source patching, generated commits, ref changes, CI dispatch, timings, application launch or hardware device needed.

## Formatting attempts in current authoring container

```text
clang-format-22 --dry-run --Werror: command not found (127)
gersemi --check: command not found (127)
soldr cargo fmt --check: command not found (127)
soldr cargo clippy --all-targets -- -D warnings: command not found (127)
```

These are pending gates, not passes. Before upstream promotion require native OFF/ON, format/lint and attention to fork #85: its SIMD noise-gate experiment must incorporate the intentional zero-hysteresis behavioural fix into its parity oracle once accepted. Do not edit that other branch here.

## Fixture refinement after source review

Test correction `d7e9ff1e8cfe115b95cd4d2a3a35f045ec4faa9e` ends the 128-sample opening burst **before** sending silent frames. The earlier fixture checked `is_open` after a mixed 1,024-sample block containing silence, which could correctly close the fixed gate and create a false GREEN failure. The isolated Clang -O2 RED and GREEN reproduction was rerun with this exact block separation: RED `failures=20`, GREEN `failures=0`. Full native test execution and format checks remain outstanding.
