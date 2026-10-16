# VideoToolbox: prevent overflow in kbps to bps conversion

Private fork review based on `88a89d7445aaf87a2445452aba97cf72642753ef`. No change to upstream refs, encoded payload, public ABI, settings defaults or CI.

## Unposted issue draft

Title: bug: mac-videotoolbox: high bitrate wraps signed VideoToolbox bits-per-second setting

Trigger: set an ABR or CBR bitrate higher than 2,147,483 kbps. The VideoToolbox GUI permits up to 10,000,000 kbps, even though its integer bits-per-second property cannot hold the scaled value.

File: `plugins/mac-videotoolbox/encoder.c`, `session_set_bitrate()`, the `new_bitrate * 1000` argument to `session_set_prop_int`.

Expected: reject values outside the signed 32-bit bps range and never configure an incorrectly wrapped rate. Actual: signed multiplication overflows; for 10,000,000 kbps, 32-bit wrap is 1,410,065,408 bps rather than the requested 10,000,000,000 bps (which cannot fit). Signed overflow is undefined C behavior.

Severity: medium, incorrect bitrate or failure to configure the encoder. The requested bitrate is a local settings input, not a remotely controlled memory-safety exploit.

## Source fix and tests

- RED `259eaa17330d49bdf85e66b0be0c6ea84e7238ce`: new native CTest includes the full VideoToolbox encoder translation unit and intercepts only `VTSessionSetProperty`. It examines actual CFNumber bps values and counts setter calls. It exercises 6,000 kbps, the largest safe value, just-above-boundary, GUI maximum, negative input and a CRF/quality-only path.
- GREEN `3f9b4be9bbce513c0ba273c9300f49f246efc074`: reject negative or above `INT32_MAX / 1000` only when an integer bitrate property will be assigned. Do not interfere with Apple's CRF quality-only path. Include stdint.h for INT32_MAX.
- Final `review:` commit: documentation and runner only.

Expected RED native assertions (not yet executed with Apple's framework):

```text
oversized_bitrate_rejected_before_conversion: invalid bitrate accepted
oversized_bitrate_must_not_submit: invalid bitrate reached VT property setter
```

### Observed standalone C control-flow regression

Clang 17 x86-64, `-std=c11 -Wall -Wextra -Werror`. The isolated test follows the exact integer expression with a stub property setter. It is **not** the full VideoToolbox translation-unit test.

```text
RED -O2: input=2147484 result=0 calls=1 scaled=-2147483296
         input=10000000 result=0 calls=1 scaled=1410065408
         input=-1 result=0 calls=1 scaled=-1000
         video bitrate guard: failures=3
GREEN -O2: video bitrate guard: failures=0
GREEN -O1 ASan/UBSan: video bitrate guard: failures=0
```

### Mac gate

From the correct fork review checkout, supply configured `macos-ci` Rust OFF/ON build directories:

```sh
uv run test/review/vt-bitrate-bounds/run-macos.py OFF_BUILD ON_BUILD
```

The runner runs format and Rust lint, builds only libobs and this one CTest target, discovers the test executable from CTest, verifies its location, adds absolute executable/framework/obs-deps rpaths, ad-hoc signs that executable with disabled library validation and runs ten repeats per mode. No app, capture device, encoder, benchmarks, branch mutation, new commits or CI dispatches.

### Required authoring-environment format attempts

```text
clang-format-22 --dry-run --Werror: command not found (127)
gersemi --check: command not found (127)
soldr cargo fmt --check: soldr not found (127)
soldr cargo clippy --all-targets -- -D warnings: soldr not found (127)
```

Do not present those as passing. Real native compilation and settings acceptance still need Mac confirmation. This change is independent of the CFNumber type fix (#89), DataRateLimits status fix (#90) and live encoder-update settings bug.
