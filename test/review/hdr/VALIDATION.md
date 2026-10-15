# Validation receipts, 2026-10-15

## Original local mathematics and verifier

Environment: x86-64 Linux, Clang 17, FFmpeg 7.1.5. Native OBS and Apple frameworks unavailable.

The test-first identity/no-op curve compiled, then failed the bounded/monotonic output assertion at test_hdr_recording_profile.c:52. With the implementation, both ASan/UBSan and -O2 -DNDEBUG runs passed 1,020,102 curve/colour cases, profile limits, mix keys, continuity and bypass. These runs include the final 400-to-1,000-nit peak range.

The exact effect-function source compiled as C++ arithmetic passed 360,000 comparisons in sanitized and optimized builds. This tests arithmetic, not the OBS shader compiler or GPU.

Seven file-verifier tests passed. An independently encoded libx265 HLG/hvc1 MP4 with AAC stereo passed the general format check with missing ambient metadata explicitly waived. An earlier conflicting-tag fixture was rejected. This is a validator fixture, not output from the OBS implementation.

## Mac-agent receipt on the original patched tree

Source: fork PR62 comment 6187564373, posted 2026-10-15. Review HEAD `afa7e76755285fb317ea842874339ac3831c9d95`; `integrate.py` edits applied in a detached worktree.

Reported environment: Apple Silicon, macOS 27, Xcode 27.0, macos-ci, RelWithDebInfo. CPU checks passed. The full app, mac-videotoolbox, obs-outputs and cmocka tests built with Rust OFF and ON. Each CTest run reported 141/141 with the expected test_svt_av1 skip. The 1,878 exported libobs symbols matched.

The Mac used local setup repairs: ad-hoc signing and absolute runtime paths for test executables, an executable-directory rpath for test_config_close_fault, and builds of test_virtual_cam_shutdown and test_config_migration. These repairs were external to the original reviewed runner. No actual recording was made.

## New preparation/runtime automation

Observed in this Linux authoring container:

```
uv run --no-project python test/review/hdr/test_native_handoff.py
Ran 9 tests
OK
```

Disposable-repository tests confirm a successful detached commit, unchanged owner branch, an exact committed file list and hash manifest, and refusal of attached HEAD, staged/unrelated edits or unexpected content. Additional tests check source-drift detection, registered-test selection, missing-executable refusal and runtime paths containing spaces. Bash syntax passes for the revised runner.

The real seven-file HDR native commit has not been generated or published here. `run.sh --prepare` or `--native` now performs that work in a detached checkout before native validation. The original transformation remains unchanged. Tests of the automation use synthetic fixture repositories; their commits are not HDR implementation commits.

## Outstanding

The revised preparation/signing automation, generated native-source commit and native OFF/ON build require a Mac rerun. Actual GUI operation, bundled GPU shader execution, HDR recordings, lifecycle/failure behaviour, simultaneous SDR output, iPhone playback and Instagram/Facebook upload acceptance remain unverified. No completed HDR integration, end-to-end compatibility, visual-quality or speed claim is made.
