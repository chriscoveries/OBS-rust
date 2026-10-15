# Caption wire-contract and decoder refinements

Fork-only owner review, based on the current caption draft head `9a812e8e1fb45112cd7a2824b157b7c0e070d90f`. The atomic-word fix already on that head is retained. Do not push to the live upstream-facing branch, merge there, or post upstream until approved.

## What changes

1. `parse_control` now requires the channel-1 high byte instead of masking arbitrary text/channel bits into a control. `T,` previously became erase-display-memory.
2. Position preambles set the indent selector and compute parity after every payload bit is present. This deliberately fixes the same defect in the vendored C expression. The default bottom-row PAC changes from `0x94e0` (white-style column zero) to `0x9470` (indent-zero), which has the same intended visible position. Re-decode emitted samples independently before promotion.
3. The test screen suppresses repeated controls, recognises preambles correctly and derives the recovered transcript only from characters that reached the screen. It no longer scans raw bytes behind the decoder's back. Five new screen assertions preserve exact RU2/RU3 windows and punctuation, and reject lost/overflowed text.
4. Five primitive tests exhaust the 16-bit input space and 960 valid row/column/underline combinations. No C API, ABI, scheduling interface or dependencies change.

## Run on the configured Mac

```sh
git fetch origin review/caption-contract-20261015
git worktree add ../obsrust-caption-contract origin/review/caption-contract-20261015
cd ../obsrust-caption-contract
bash test/review/run-caption-contract.sh
```

Expected inventory: 31 tests (existing 21 plus 10 new). Counts are a source inventory, not a claimed test result. The runner saves exact HEAD, debug/release, clippy and formatting logs.

Test-first primitive checkpoint: `7afed70684876345e3daea6d735b9f7fd6e3b85a`. Run `soldr cargo test -p obs-captions --locked --test eia608_contract` there in a separate worktree. Four new primitive tests are expected to fail. After the fix, every primitive test and existing atomic-rejection test must pass. Do not replace a failed test with a skip.

## Evidence and limits

The base eia608.rs and rollup_decode.rs were materialised from connector text and checked against Git blob IDs. An independently compiled C arithmetic probe reproduces 960 old control-classification mismatches over 65,536 inputs, and 480 parity failures / 960 missing-indent-bit cases over 960 preambles. The corrected expressions have zero failures under Clang ASan/UBSan. This is a bit-level probe; the Rust crate has not been compiled in this environment because neither Rust nor soldr is installed. No claim of an observed Rust GREEN is made.

The minimal test screen only models the channel-1 roll-up subset. A complete libcaption/FFmpeg decoder pass is still required, particularly for the changed PAC bytes. Fractional frame rates, draining/queue bounds, flush/clear, extended characters and OBS integration remain separate work.
