# VideoToolbox colour dictionary: owner review

## Unposted issue draft

Title: mac-videotoolbox: SDR session setup includes NULL colour properties

Trigger: initialize a VideoToolbox encoder with a non-PQ/non-HLG colour space.

Location: plugins/mac-videotoolbox/encoder.c:352-403 at 087a25c7d977ad3fbcc12a572d95ec05042824de; CFDictionaryCreate at lines 385-386.

Expected: construct and submit the three populated SDR colour properties. Actual: both arrays have five entries, but their final two entries remain NULL for SDR and all five are passed to CoreFoundation. With CF type callbacks this reaches CFHash on NULL in the tested implementation. Severity: high, local encoder initialization failure; not an external-payload finding. Native macOS reproduction remains required.

Duplicate searches across open and closed upstream issues/PRs for session_set_colorspace and CFDictionaryCreate returned no result. This is a focused search, not an exhaustive proof of uniqueness.

## Source scope

The count is now three for SDR and five for PQ/HLG. Existing HDR metadata values and property-error propagation remain unchanged. The production diff also removes one blank line in convert_sample_to_annexb; it has no semantic effect. No public API, workflow, default, or other ownership fix is included.

RED commit: a31309cbf687cdff3a5670c0718ccac68df7a408.
GREEN commit: 708584841f79a67d50fb74b70892dc38ce652f43.
Original encoder blob: 32f39f1bca7cb321977f1862228243a7a811af8d.
Fixed encoder blob: 1be5227336e6b08ddef250729fe165e85b56c96c.

## Headless native test

The test includes the actual complete encoder translation unit. CoreFoundation constructs and inspects real dictionaries. Only dictionary input validation, the session property setter and the global HDR peak query are intercepted. No actual compression session, graphics device, capture input or OBS application is started.

Six colour spaces are tested with both successful and failing session setters. It verifies populated counts, primaries, transfer function, matrix, retained HDR data lengths and error propagation. Invalid NULL input is reported before constructing a safe prefix so RED gives assertions rather than terminating inside CoreFoundation.

Configure two normal macos-ci test builds from this source checkout with ENABLE_RUST_LIBOBS OFF and ON, respectively. Then:

```sh
uv run test/review/vt-colour-dictionary/run-macos.py OFF_BUILD ON_BUILD
```

The runner builds only libobs and test_vt_colour_dictionary, selects the test executable through CTest, adds absolute executable/framework/obs-deps rpaths and signs that executable ad hoc before ten CTest repetitions. It does not configure, patch, commit, push, change refs, dispatch CI or run timings. Rerun at the RED commit manually for a native failing receipt.

## Observed local evidence

Linux x86-64, Clang 17, installed Swift CoreFoundation implementation. The local probe compiles the unchanged production colour helper functions with distinct stand-in VideoToolbox key constants and an intercepted setter. This is not compilation of the native SDK translation unit.

A separate real CFDictionaryCreate call with CF type callbacks and a three-value prefix plus two NULL entries aborted in CFHash (exit 132). Passing exactly three entries succeeded. This observation applies to the available Linux CoreFoundation implementation, not a measured macOS crash.

```text
Original helper: videotoolbox colour dictionary: checks=144 failures=16
exit=1
Fixed, -O2 -DNDEBUG: videotoolbox colour dictionary: checks=136 failures=0
exit=0
Fixed, -O1 -g -fsanitize=address,undefined:
videotoolbox colour dictionary: checks=136 failures=0
exit=0
```

RED assertions:

```text
colour_dictionary_count: wrong number of populated properties
sdr_properties_have_no_null_entries: NULL key or value passed to CFDictionaryCreate
```

Compiler options included C11, -fblocks, -Wall -Wextra -Werror. The test runner passed Python bytecode compilation. The GitHub source diff was read back before publication.

## Required formatting output

```text
$ clang-format-22 --dry-run --Werror plugins/mac-videotoolbox/encoder.c test/cmocka/test_vt_colour_dictionary.c
clang-format-22: command not found
exit=127
$ gersemi --check test/cmocka/tests.d/test_vt_colour_dictionary.cmake
gersemi: command not found
exit=127
$ soldr cargo fmt --check
soldr: command not found
exit=127
$ soldr cargo clippy --all-targets -- -D warnings
soldr: command not found
exit=127
```

These checks remain blocked. Available format tools were searched for and package retrieval failed on DNS. No Rust code changed. Run the required checks and native OFF/ON tests before upstream promotion. Fork-only draft; no upstream-connected branch or discussion is changed.
