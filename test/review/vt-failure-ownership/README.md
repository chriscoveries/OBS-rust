# VideoToolbox failure ownership

Fork-only review based on `087a25c7d977ad3fbcc12a572d95ec05042824de`. This is a prerequisite cleanup separated from HDR colour/session controls. The missing-pixel-buffer-pool bool repair remains in its own draft. No upstream-connected branch, public header, encoded payload, colour metadata or workflow changes.

## Source change

Only `sample_encoded_callback` and `create_encoder` change: 15 added and 8 removed production lines. The complete ordinary encoder source stays in place. There are no function-body include swaps, runtime patches or integration generators.

- Session creation failure now returns its original status before accessing session properties. Temporary specification dictionaries are released first.
- A successfully created session is assigned to its owning encoder before property setup. Any later setup failure is therefore cleaned by the existing destructor.
- An error callback releases its non-null input frame reference.
- Failed queue insertion balances the extra retained output reference. Successful insertion retains the existing ownership transfer.
- An absent optional callback source is never passed to CFRelease.

Original encoder blob: `32f39f1bca7cb321977f1862228243a7a811af8d`.
Fixed encoder blob: `6ac95f599cd78f8d79d61e65a7a424b0b9f74df7`.
The uploaded fixed blob matches the source used in the local probes.

## Test-first history

RED: `14ad1aa3773637c0b5a1849ca682e40f919b3832`.
GREEN: `9db28d0b6b98c4b04c8a5b6e5cdaf5022ec61f1e`.
This final review commit contains only the handoff and native runner.

Observed RED assertion output:

```
failed_callback_releases_source: error callback leaked its source-frame reference
full_queue_releases_retained_sample: failed enqueue leaked the retained output sample
absent_source_is_not_released: callback attempted to release a NULL source
videotoolbox callback ownership: checks=12 failures=3
exit=1

failed_creation_stops_before_session_use: creation error was replaced by a later property error
failed_creation_stops_before_session_use: failed creation continued into session properties
failed_creation_stops_before_session_use: failed creation used an invalid session
failed_setup_keeps_session_owned_for_cleanup: created session was lost before setup failure cleanup
failed_setup_keeps_session_owned_for_cleanup: destructor did not invalidate and release the session once
videotoolbox session failure: checks=9 failures=5
exit=1
```

Observed GREEN in both optimized and ASan/UBSan builds:

```
videotoolbox callback ownership: checks=12 failures=0
videotoolbox session failure: checks=9 failures=0
exit=0
```

Clang 17, x86-64 Linux, C11, -Wall -Wextra -Werror. Optimized flags: -O2 -DNDEBUG. Sanitized flags: -O1 -g -fsanitize=address,undefined.

## Evidence boundary

Local probes compile the exact callback, constructor, destructor and required property-wrapper functions from the pinned source with framework/OBS interfaces mocked. The native tests include the complete encoder translation unit and use real framework headers. Native compilation/linkage and real Apple reference behavior have not run here. The checked-in tests control only failure results and tracked references; they do not start a real encoder, capture device or GPU.

Both CTest targets are registered for macOS and can run with libobs Rust OFF and ON. Native checks must pass before promotion. Successful recording, asynchronous shutdown/drain behavior, queue destruction and synchronous submission-failure cleanup are outside this focused change.

## Native command

Use two configured build directories belonging to this source worktree:

```
bash test/review/vt-failure-ownership/run-macos.sh OFF_BUILD ON_BUILD
```

The runner builds only libobs and the two tests. It requires both named tests in CTest, refuses executable paths outside the supplied build, adds absolute executable/framework/dependency rpaths and signs only the tests ad hoc with disable-library-validation. It repeats both tests ten times per mode. It creates no commits, patches no source, changes no branch refs and runs no network service tests. No DYLD environment override is used.

## Required format-check output

```
$ clang-format-22 --dry-run --Werror plugins/mac-videotoolbox/encoder.c test/cmocka/test_vt_callback_ownership.c test/cmocka/test_vt_session_failure.c
bash: line 1: clang-format-22: command not found
exit=127
$ gersemi --check test/cmocka/tests.d/test_vt_callback_ownership.cmake test/cmocka/tests.d/test_vt_session_failure.cmake
bash: line 1: gersemi: command not found
exit=127
$ soldr cargo fmt --check
bash: line 1: soldr: command not found
exit=127
$ soldr cargo clippy --all-targets -- -D warnings
bash: line 1: soldr: command not found
exit=127
git diff --check: exit=0
bash -n on runner: exit=0
```

The pinned tools are unavailable; package retrieval failed on DNS. These are outstanding gates, not passes. No Rust source changes are included.

## Unposted issue drafts

### mac-videotoolbox: stop after session creation failure and retain setup ownership

Trigger: VTCompressionSessionCreate fails, or a newly created session rejects its initial properties.
Location at the base: plugins/mac-videotoolbox/encoder.c, create_encoder, approximately lines 510-618.
Expected: preserve the first creation error without session access; release a created session on later setup failure.
Actual: initialization falls through after failed creation; later failures lose the created session because enc->session is assigned only at the end.
Severity: high for failed initialization, with invalid session use and loss of the original diagnostic; medium for retained sessions after setup failure. No remote trigger claimed.

### mac-videotoolbox: balance frame references on callback and queue errors

Trigger: an error callback or a full output queue.
Location at the base: plugins/mac-videotoolbox/encoder.c:403-424.
Expected: release the callback-owned source and retain an output reference only when the queue accepts it.
Actual: the error return leaks the source; failed queue insertion leaks the extra output reference.
Severity: medium, resource loss during local encoder failure/backpressure.

Scoped searches of open/closed upstream VideoToolbox PRs and VTCompressionSessionCreate reports found no matching repair. The HDR signalling proposal and compiler-warning work are separate. No issue was posted upstream.
