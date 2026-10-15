# Exact noise-gate SIMD review

This is a small independent slice of fork issue #27. It starts at 087a25c7d977ad3fbcc12a572d95ec05042824de and carries no commits from the earlier approximate dynamics branches. The compressor, limiter, expander and public API are unchanged. Nothing is posted upstream.

## Commit sequence

- Characterization: a8d986121bc259b9c3e702dc9fcd9da89997930a. Ordinary private-state/scalar extraction plus original-loop differential tests.
- RED: 67d8854ea6af74e2c26ec314963a1a3ddc00d3c0. Enable the vector-path assertion.
- GREEN: 5d3911a7b57c7f209f17aad0aa18b69ae480fcd6. SSE2 level reduction and output gain, with the original sequential state machine.
- This final review commit contains the runner, adapter and raw indicative measurements. Exclude review-only files when preparing an upstream change.

## Observed tests

```
characterization: noise_gate_process: checks=324215 failures=0
RED: sse2_processes_disjoint_full_vectors: expected 8 SIMD frames
RED: noise_gate_process: checks=324216 failures=1
RED: exit=1
GREEN Clang 17 optimized: noise_gate_process: checks=324216 failures=0
GREEN Clang 17 ASan/UBSan: noise_gate_process: checks=324216 failures=0
GREEN GCC 14.2 optimized: noise_gate_process: checks=324216 failures=0
scalar fallback (-U__SSE2__): noise_gate_process: checks=324215 failures=0
minimal CMake driver: test_noise_gate_process 1/1 Passed
```

Mutation checks independently disabled the overlap guard (13 failures), the NaN fallback (2,627 failures), or changed the close decision to use the current instead of previous level (117,777 failures). Each exited 1. The production source was restored afterward.

Samples, untouched tails and carried state compare bit-for-bit against the original scalar loop, including quiet spans, threshold equality, zero-time parameters, inverted thresholds, signed zero, subnormals, infinities, quiet NaNs and overlapping channel planes. Floating-point exception flags and signaling-NaN traps are outside this evidence. The minimum CMake check does not build the full OBS module.

## Correctness only

```
bash test/review/noise-gate/run.sh --check
```

For SSE2 coverage on Apple Silicon with Rosetta already installed:

```
arch -x86_64 /bin/bash test/review/noise-gate/run.sh --check
```

Native arm64 exercises only the scalar fallback. The direct standalone checks have no external dylib dependency and invoke no CTest. A full module/CTest check on the owner's Mac remains required; that check must add absolute test rpaths and ad-hoc signing as required by the review rules. No runner creates commits, modifies branch refs or dispatches CI. Timing is never the default.

## Indicative speed-up (not a measurement on the reviewer's hardware)

CPU: AMD EPYC 9V74 80-Core Processor; this Linux container exposes 5 logical CPUs and affinity permits 5. OS: Linux 6.18.44. Compiler: Clang 17.0.0. Flags: -std=c11 -O2 -DNDEBUG -fno-fast-math -ffp-contract=off.

The adapter reuses tools/ab_bench/core.py byte-for-byte from fork #70 at 2867f3bdc2bc85c3c41606bbc20d37429feacddf, blob 9fc69edd742c3c10c247da7cbfd0d0fbca10aeb9. The shared row key frames is the number of 1,024-sample audio blocks here. The measured production header is blob a2b04dc2e5b00eae36164b4255871e9458d2b812.

Nine interleaved AB/BA rounds per case; each side warms up for 1,024 blocks and measures 8,192 blocks. Input restoration is outside the timed interval. State is carried across blocks. Output and carried-state equality is checked before timing; final output/checksum equality is checked per round. Raw data is in results/rounds.jsonl.

| 48 kHz case | Scalar median (IQR), ns/block | SSE2 median (IQR), ns/block | Median paired ratio | Overlapping IQR |
|---|---:|---:|---:|---|
| Stereo | 7174.35 (485.18) | 4874.94 (135.04) | 1.4553x | No |
| 5.1 | 8596.18 (781.38) | 4479.10 (261.29) | 1.9661x | No |

Aggregate whole-machine busy load: before 0.0%; during median 20.31%, range 18.75% to 21.88%. During-load includes this benchmark. It is not a separate measurement of background processes. No process identities were collected. These are indicative measurements on one virtualized Linux environment, not predictions for a Mac or end-to-end OBS results.

For the noise-gate portion alone of a stereo microphone plus 5.1 desktop pipeline: 48,000 / 1,024 = 46.875 blocks per second per track. Estimated saving = ((7174.3472 - 4874.9386) + (8596.1787 - 4479.0997)) * 46.875 / 1,000,000 = 0.30077 CPU-ms per second. At 60 fps this is 0.005013 CPU-ms per frame, or 0.03008% of a 16.6667 ms frame budget. Compressor, limiter, video scaling and GPU copies are not included. This is a microbenchmark-derived estimate, not an end-to-end measurement.

## Required checks still blocked

```
clang-format-22 --dry-run --Werror: command not found; exit=127
gersemi --check: command not found; exit=127
soldr cargo fmt --check: command not found; exit=127
soldr cargo clippy --all-targets -- -D warnings: command not found; exit=127
```

Pinned formatter installation was attempted and failed on package-host DNS resolution. No formatter success is claimed. No Rust source changes are present. Full OBS OFF/ON module builds, native macOS/Windows execution and project formatting remain required before upstream promotion. Recommendation: keep improving until those gates pass; the gain is useful but modest in the full streaming budget.
