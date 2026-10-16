# VideoToolbox: frame submission failure must release the pooled buffer

**Fork review.** Source base: `88a89d7445aaf87a2445452aba97cf72642753ef`, the upstream master revision verified when work began. No change to upstream refs, workflow configuration, public OBS API, encoded payload, colour parameters, or timing.

## Unposted issue draft

Title: mac-videotoolbox: failed frame submission retains its pooled pixel-buffer reference

Trigger: `CVPixelBufferLockBaseAddress`, `CVPixelBufferUnlockBaseAddress`, or `VTCompressionSessionEncodeFrame` returns a nonzero status during an ordinary hardware/software encode.

File: `plugins/mac-videotoolbox/encoder.c`, `vt_encode()` failure return following `VTCompressionSessionEncodeFrame`.

Expected: a pooled pixel buffer acquired for that frame is released when submission fails. After successful submission, the output callback releases the buffer exactly once, whether called synchronously or later. Actual: the failure label returns false without releasing its newly allocated `pixbuf`. Repeated failures retain frames and consume memory/pool capacity.

Severity: medium resource leak and degradation on failed encoding. The observed reproduction uses deliberately failed framework calls; no remote-triggerability or memory exploitation is asserted.

Apple VT's API states the frame callback receives `sourceFrameRefCon`; the encoder retains accepted image buffers as needed. Chromium's VideoToolbox encode path likewise hands callback ownership across only after a successful submission:
- https://developer.apple.com/documentation/videotoolbox/vtcompressionsessionencodeframe
- https://chromium.googlesource.com/chromium/src/+/e102d7cb9bd8a6b610ca361cd9f07a7d434e9af6/media/gpu/mac/vt_video_encode_accelerator_mac.mm

## RED then GREEN

- RED `97a97f0ec405150a8da0efea2f00249f5c4245c4`: new headless native CTest, full production encoder translation unit with a real CoreVideo 16×16 NV12 buffer and targeted mocked operations. It creates a retained observer reference so it can measure the frame's lifetime even when the production reference is released.
- GREEN `ae356f2a69eb696dd86ec65e6bbde19c80c656d0`: on the `vt_encode` failure label, release `pixbuf` when non-NULL. No other branch is touched.
- Final `review:` commit: this document and the Mac runner only.

**Expected native RED assertions (not yet run):**

```text
failed_frame_must_release_pool_reference: leaked source frame
```

That failure should occur for three cases: failed lock, failed unlock, and rejected encode. The success case should retain its frame until the simulated output callback, then release it exactly once; a missing pool should not release anything.

## Local reproduction (observed)

Clang 17 x86-64 Linux. The isolated local test follows the exact production control flow and reproduces the failure stages with C structs standing in for unavailable Apple frameworks. It is not the macOS full-translation-unit test.

```text
RED -O2: stage=1 refs=1 expected=0
         stage=2 refs=1 expected=0
         stage=3 refs=1 expected=0
         vt_encode error cleanup: failures=3 ; exit 1
GREEN -O2: vt_encode error cleanup: failures=0 ; exit 0
GREEN -O1 ASan/UBSan: vt_encode error cleanup: failures=0 ; exit 0
```

The native test's CFGetRetainCount assertions still need Mac validation; no SDK build or encoding device test ran in this authoring environment.

## Narrow Mac validation

Check out the fork review branch from this exact commit and configure OFF and ON `macos-ci` test builds. From its root:

```sh
uv run test/review/vt-submit-ownership/run-macos.py OFF_BUILD ON_BUILD
```

The runner executes formatting and clippy first; verifies build cache identity, builds only libobs and `test_vt_submit_ownership`, discovers and bounds the executable path with CTest, adds absolute executable/framework/obs-deps rpaths, signs the local test ad hoc with disabled library validation and repeats CTest ten times per mode. It does not create commits, modify refs, launch OBS or dispatch CI.

## Required format/lint attempts

```text
clang-format-22 --dry-run --Werror: command not found (exit 127)
gersemi --check: command not found (exit 127)
soldr cargo fmt --check: soldr not found (exit 127)
soldr cargo clippy --all-targets -- -D warnings: soldr not found (exit 127)
```

These checks remain pending. Do not promote until both native modes and exact source-format checks pass. The callback ownership and session-creation repairs in fork #87 are related but separate: this fixes the synchronous *submission* side, while #87 fixes output-callback and session-initialization paths.
