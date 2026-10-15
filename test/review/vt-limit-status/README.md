# VideoToolbox: preserve rate-limit setter failures

Private fork review based on upstream master snapshot `087a25c7d977ad3fbcc12a572d95ec05042824de`. No upstream-connected branch or public API change.

## Unposted issue draft

Title: mac-videotoolbox: DataRateLimits errors silently reported as success

Trigger: set an encoder's optional max-bitrate limit when VTSessionSetProperty refuses the value with an error other than `kVTPropertyNotSupportedErr`.

Location: `plugins/mac-videotoolbox/encoder.c`, `session_set_bitrate()`, immediately after the DataRateLimits setter at the stated base.

Expected: preserve the returned OSStatus so encoder initialization fails (or dynamic update warns) when rate limits cannot be configured, except `kVTPropertyNotSupportedErr`, which remains the existing documented nonfatal compatibility case. Actual: all other errors are overwritten by the final `return noErr`; the caller believes rate limits were applied.

Severity: medium: silent unintended encoder rate control. No remote memory-safety implication.

## Two source stages

1. RED `8aa121c9ec58b1e9b2dcfab4bda1d290a5a7732a`: seven C regression scenarios in a new CTest executable that includes the real encoder translation unit and intercepts only VTSessionSetProperty.
2. GREEN `37a469c5a549dfa2d7b8d0a568a3c83e608def7f`: add `return code` after the existing special treatment of `kVTPropertyNotSupportedErr`. No other code or rate algorithm changed.
3. Final `review:` commit: this handoff and runner only.

Expected RED native failing assertion messages (not yet executed on macOS):

```text
data_rate_limit_error_is_not_masked: non-unsupported error was lost
data_rate_limit_other_error_is_not_masked: OSStatus was replaced by success
```

The other cases verify noErr, unsupported-property compatibility, an initial bitrate failure, and a disabled limit. The native test expects 21 assertions.

## Observed local reproduction

Clang 17, C11 with a complete copy of the production `session_set_bitrate` function and its actual CoreFoundation CFNumber/CFArray construction, using mock VideoToolbox status responses:

```text
RED: FAIL data_rate_limit_error_is_not_masked: non-unsupported error was lost
RED: rate status regression: failures=1
GREEN -O2 -DNDEBUG: rate status regression: failures=0
GREEN -O1 -g -fsanitize=address,undefined: rate status regression: failures=0
```

This local reproduction is not the complete native translation-unit test. That test's RED/GREEN results remain pending.

## Mac validation

Configure `ENABLE_RUST_LIBOBS=OFF` and `ON` builds for this exact checkout. Then run, from the checkout:

```sh
uv run test/review/vt-limit-status/run-macos.py OFF_BUILD ON_BUILD
```

The script runs format/lint first; builds only `libobs` plus the focused test; discovers the test executable from CTest; adds absolute paths for the executable directory, libobs.framework and obs-deps libraries; signs the local test executable ad hoc with disable-library-validation; repeats the test ten times per mode. No app, GPU, device, real VT encoder, git mutation or timing runs.

## Required formatting attempts from the authoring environment

```text
clang-format-22 --dry-run --Werror: command not found; exit 127
gersemi --check: command not found; exit 127
soldr cargo fmt --check: command not found; exit 127
soldr cargo clippy --all-targets -- -D warnings: command not found; exit 127
```

These are blocked, not passes. Native tests and formatting/lint receipts must be supplied before approval. The two-CFNumber-type fix in fork #89 and other VideoToolbox ownership fixes are independent; upstream merge order may require resolving a small one-function context conflict.
