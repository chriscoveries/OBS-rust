# Local receipt, 2026-10-15

Environment: x86-64 Linux container, Clang 17, FFmpeg 7.1.5. Native OBS and Apple frameworks unavailable.

Test-first checkpoint with identity/no-op curve stubs compiled successfully, then exited 1 at the bounded/monotonic output assertion in test_hdr_recording_profile.c, line 52. This is an observed runtime RED.

Production mapper, Clang C11, -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined:

```
PASS: 1020102 curve/color cases, profile bounds, mix-key isolation, knee continuity and bypass
```

Same test with -O2 -DNDEBUG: identical PASS. Tests rerun after narrowing the final output-peak grid to 400-1,000 reference nits to avoid a second HLG down-map.

Exact hdr-recording.effect function compiled as C++ arithmetic, sanitized and separately -O2 -DNDEBUG:

```
PASS: 360000 exact-effect/reference comparisons and disabled bypass
```

File validator:

```
Ran 7 tests
OK
```

An actual libx265 10-bit HLG/hvc1 MP4 fixture, AAC stereo, 30 fps, with frame-level colour parameters explicitly set, passed the general HLG format check. The authored amve extension was not part of this external-encoder fixture and was explicitly waived with --allow-missing-ambient. Earlier mismatched container tags were correctly rejected. Reproduce with make-verifier-fixture.sh; this fixture is not a demonstration of the OBS feature or its tone mapping.

Native patch application, native C/property tests, Qt GUI, GPU shader compilation, actual OBS HEVC recording, full OFF/ON ABI parity, iPhone playback and both Meta uploads remain NOT TESTED. No speed, image-quality, completed integration or upload-compatibility result is claimed.
