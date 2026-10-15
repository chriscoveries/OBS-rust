# VideoToolbox rate-limit number types

## Unposted issue draft

Title: mac-videotoolbox: bitrate limits reinterpret doubles as int and float

Trigger: enable Limit bitrate for an ABR encoder and set a maximum bitrate/window.

Location: plugins/mac-videotoolbox/encoder.c:330-332 at 087a25c7d977ad3fbcc12a572d95ec05042824de.

Expected: a CFArray whose CFNumber values equal max_bitrate * 125 * max_bitrate_window bytes and max_bitrate_window seconds. Actual: the storage for both values is double, but CFNumberCreate is told to read an int for bytes and a float for seconds. It reads differently typed bytes rather than performing a C numeric conversion. At 6000 kbps and 1.5 seconds the available CoreFoundation implementation constructs 0 bytes and 0 seconds instead of 1125000 and 1.5.

Severity: medium, wrong encoder configuration; no remote input or memory-safety exploit claim. The actual VideoToolbox encoder's rejection or treatment of those malformed values still requires native confirmation.

Focused open/closed upstream issue and PR searches for DataRateLimits and CFNumber returned no match.

## Two-line source fix

Both CFNumberCreate calls now specify kCFNumberDoubleType. Source storage, arithmetic, rate-control selection, disabled behavior and session ownership are unchanged. The published production diff was read back and contains exactly those two substitutions.

RED: b6f97ee2eadae6522cdcf03ce819d41f7d6978b2.
GREEN: d52baf33a9ea50588b94041bc0ce65dddb78a4b7.
Original encoder blob: 32f39f1bca7cb321977f1862228243a7a811af8d.
Fixed encoder blob: 29b472ed6e953423f1291eaabf5aaf84e9d9e7f4.

## Primary API contract

CFNumber.h specifies that the type parameter describes the numeric object pointed to by valuePtr. It also distinguishes the storage format returned by CFNumberGetType from the input type. The regression checks numeric values, not CoreFoundation's internal storage choice.

https://github.com/swiftlang/swift-corelibs-foundation/blob/main/Sources/CoreFoundation/include/CFNumber.h

## Observed tests

Local environment: x86-64 Linux, Clang 17, installed Swift CoreFoundation. Actual CFNumberCreate, CFNumberGetValue and CFArrayCreate run; only VideoToolbox keys and VTSessionSetProperty are replaced for the local probe. The production session_set_bitrate and its helper functions are compiled directly, with the inactive macOS availability branch represented as available. ABR does not enter that branch.

Sixteen maximum-bitrate/window combinations cover 50, 6000, 12000 and 10000000 kbps with 0.1, 0.35, 1.5 and 10 seconds. A disabled-limit guard verifies no DataRateLimits submission. All comparisons use the actual constructed CFNumbers.

```text
RED: videotoolbox rate limits: checks=81 failures=32
exit=1
GREEN -O2 -DNDEBUG: videotoolbox rate limits: checks=81 failures=0
exit=0
GREEN -O1 -g -fsanitize=address,undefined:
videotoolbox rate limits: checks=81 failures=0
exit=0
```

Failing assertions:

```text
rate_limit_bytes_match_settings: encoded byte limit differs from requested limit
rate_limit_window_matches_settings: encoded window differs from requested seconds
```

Flags include -std=c11 -fblocks -Wall -Wextra -Werror. This proves the number values submitted by the helper; it is not an actual encoder rate-control measurement.

## Native gate

The checked-in test includes the complete encoder translation unit, real macOS SDK headers and libobs. It replaces only VTSessionSetProperty. It needs no real encoder session, GPU or running OBS application. Native SDK compilation and OFF/ON linkage remain unexecuted here.

Configure normal macos-ci test builds for this checkout with Rust OFF and ON, then:

```sh
uv run test/review/vt-rate-numbers/run-macos.py OFF_BUILD ON_BUILD
```

Only libobs and test_vt_rate_limit_numbers build. The runner verifies each source/build relationship, obtains the executable from CTest, adds absolute executable/framework/obs-deps rpaths and ad-hoc signs this test executable before ten repetitions. No source rewriting, commits, ref updates, DYLD overrides, CI dispatch or timings. Python bytecode compilation passed locally.

## Required formatting attempts

```text
$ clang-format-22 --dry-run --Werror plugins/mac-videotoolbox/encoder.c test/cmocka/test_vt_rate_limit_numbers.c
clang-format-22: command not found
exit=127
$ gersemi --check test/cmocka/tests.d/test_vt_rate_limit_numbers.cmake
gersemi: command not found
exit=127
$ soldr cargo fmt --check
soldr: command not found
exit=127
$ soldr cargo clippy --all-targets -- -D warnings
soldr: command not found
exit=127
```

Tool retrieval was blocked by DNS. These checks remain outstanding; no Rust files changed. Fork-only owner review. Keep the separate SDR dictionary and session-ownership repairs separate from this two-line patch.
