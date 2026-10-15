# macOS Tier 2 Xcode/SDK selection: owner review

This is a fork-only draft on the 2026-10-14 upstream-compatible baseline `a48a9a13d80d3ac5ded749093e6ac2a08855e189`. The original upstream branches and CI are unchanged.

## Rationale
Full-app and release workflows already select `/Applications/Xcode_26.5.app/Contents/Developer`. The separate `rust-tier2` job inherits an image-dependent default, which can resolve its SDK from Command Line Tools. Force the same Xcode only for the Tier 2 job, and validate that the SDK returned by `xcrun` actually comes from that Xcode before compiling or linking.

## Exact Mac-agent command
```bash
git fetch origin review/tier2-macos-sdk-20261015
git worktree add ../obsrust-review-tier2 origin/review/tier2-macos-sdk-20261015
cd ../obsrust-review-tier2
git submodule update --init --recursive
bash test/review/run-tier2-macos-sdk.sh
```

The first check is a mocked functional test that needs no Xcode and rejects CLT fallback, nonexistent Xcode and nonexistent SDK. On a Mac, the runner then executes both OFF and ON build/test configurations and symbol-export parity. Preserve the selected Xcode/SDK lines and build logs.

## Expected RED
On the first test-only commit, `test_tier2_xcode_sdk.sh` fails because `select-macos-sdk.sh` is absent. The GREEN commit adds the helper, CI selection and its use in `validate.sh`. The native Mac failure mode was intermittent; do not claim proven elimination until the CI has run repeatedly on affected runners.

## Scope / safety
No new workflow files, no changes to global `xcode-select`, no changes to Windows or Linux. The `DEVELOPER_DIR` workflow environment variable and explicit `CMAKE_OSX_SYSROOT` are macOS-specific.

Owner permission is required before anything is posted to the upstream project.
