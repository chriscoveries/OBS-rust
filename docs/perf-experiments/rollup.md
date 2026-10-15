# Experimental performance rollup

**Review index only; not for merge.** Updated 2026-10-16. Do not promote any performance work upstream without owner approval and source-pinned correctness/bench receipts. This index makes no app-level speed claim.

## Current independent items

| Item | Branch / canonical review | Head SHA | Base SHA | Actual tests and RED/GREEN | Indicative speed-up | Confidence / recommendation |
|---|---|---|---|---|---|---|
| P1 Direct-output scaler | [Upstream draft #743](https://github.com/obs-rust/obs-studio/pull/743), [reviewer benchmark #70](https://github.com/chriscoveries/obsrust-fork/pull/70) (`bench/ab-scaler`) | #743 `8b9a65bdaa2eff4e57e3931cc8bf90964f9d5ae1`; #70 `2867f3bdc2bc85c3c41606bbc20d37429feacddf` | #70 `8b9a65bdaa2eff4e57e3931cc8bf90964f9d5ae1` | 60/60 Mac OFF/ON alias/scaler test repetitions on an earlier head; 12/12 before/after pixel parity and corrected `--dry-timing` on the current #70. Alias RED failed, GREEN passed. | **Unmeasured.** Direct path eliminates one destination scratch-to-output copy when strict format/layout/alias guards pass. | Correctness high; speed unknown. **Keep improving** with an independently run timing receipt. |
| P2 SIMD dynamics | [Clean noise-gate slice #85](https://github.com/chriscoveries/obsrust-fork/pull/85), `fix/review-noise-gate-equal-threshold` [#91](https://github.com/chriscoveries/obsrust-fork/pull/91) separate scalar bug | #85 `d4ccac3f845734b6f918f0a9170092999556bf26` | `087a25c7d977ad3fbcc12a572d95ec05042824de` | RED: vector counter 0 vs expected 8. GREEN: 324,216 bit-exact samples/state checks; Clang optimized/ASan+UBSan and GCC; 1/1 minimal CTest. Nine alternating timing rounds. | **Measured indicative kernel win:** stereo **1.4553×**, 5.1 **1.9661×** on noise-gate process only. Not an end-to-end speed-up. | Parity high on sampled cases, microbench medium. **Keep improving**, native OFF/ON and lint pending. |
| P3 Rust dispatch plumbing | `perf/54-media-dispatch` | `670cf29e334e7ff3855f1bd50ccaf3ed70710eb7` | Historical, hundreds of commits behind current upstream | Tier-selection unit tests exist on old branch; nothing calls the crate. No current HEAD/merged-tree validation and no code-size/per-call baseline. | **No measurable kernel gain**: zero callers. Dispatch overhead unmeasured. | Benefit unproven. **Drop/park** until one real hot Rust kernel needs it. |
| P4 IOSurface → VT texture | [Upstream issue #755](https://github.com/obs-rust/obs-studio/issues/755); `perf/47-m1-core` → `perf/47-m1-vt` → `perf/47-m1-evidence` | `48b945d599fef4ecf53e4ed76605aafbf6160e75` → `78b0fb0a735fdd558385b8464060b578aaaf2ba8` → `c8595c1ece499af05aaaec74fc451abb8c97450a` | Historical prior to current master; stale and conflicting | Old combined prototype counted 120/120 accepted texture submissions at zero host-copy bytes, but split evidence branch cannot reproduce raw runs owing to double reservation; core lease branch has a stop/start lifetime race. No validated current stack. | **No timing**. 1080p NV12 raw path: 3,110,400 bytes copied/frame; 186,624,000 bytes/s at 60 fps. Texture route's host-side counter = 0 on older prototype only. | Correctness insufficient. **Park** pending fixes, genuine Mac lifecycle tests and maintainer direction. |

For P1 the later head of upstream #743 is the correct source, not fork #44. #70 supersedes the separate experimental timing scripts in #67 for reviewer A/B work. Do not reuse `bench/harness` or copy its machine-identifying material.

For P2, do **not** reuse the old `perf/27-dynamics-simd` commits with historical co-author trailers. The three arithmetic repairs have separate earlier fork drafts (#43, #60, #64), but #85 is the cleanly based, exact-parity **noise-gate-only** version. Existing compressor, limiter and expander SIMD claims are not covered by #85. The scalar equal-threshold bug in #91 is independent and must be incorporated as intentional parity correction if both changes are accepted.

## Measurement inventory and limitations

### P1 scaler

- Formats: BGRA, I420, NV12, P010 × same-size, upscale, downscale. `--quick` confirms 12 before/after pixel identities; `--dry-timing` exercises the report parser but explicitly **does not** produce performance evidence.
- Benchmark implementation: [fork #70](https://github.com/chriscoveries/obsrust-fork/pull/70) uses nanosecond time on macOS, calibrated batches ≥10 ms, interleaving, host-load sampling, median/IQR and an inconclusive flag.
- Missing: quiet-host `--measure` raw JSONL, workload-specific ns/frame, which layouts actually enter direct output in production, per-conversion-frame memory bandwidth, end-to-end OBS CPU evidence.
- A single scratch-output memcpy on NV12 1080p handles **3,110,400 bytes per frame**. Avoiding it prevents that copy's memory traffic; it does not automatically mean a 3.1 MB reduction in resident allocation, because the scratch buffer remains allocated. No ns/frame or percentage gain is established.

### P2 noise gate: the only usable new kernel timings

Host recorded in #85: AMD EPYC 9V74 80-Core Processor, five logical CPUs visible, affinity five; Linux 6.18.44, Clang 17.0.0; `-std=c11 -O2 -DNDEBUG -fno-fast-math -ffp-contract=off`. Background busy load 0.0% before; during benchmark median 20.31% (18.75–21.88%). Nine interleaved AB/BA rounds after warm-up. Each side processes 8,192 blocks × 1,024 samples per round. Equal output and carried state checked before timing; checksum parity checked every round. Raw rounds and receipt are committed with [#85](https://github.com/chriscoveries/obsrust-fork/pull/85).

| 48 kHz workload | Scalar median ns/block (IQR) | SSE2 median ns/block (IQR) | Paired median speed-up | IQR intervals overlap? |
|---|---:|---:|---:|---|
| Stereo | 7,174.35 (485.18) | 4,874.94 (135.04) | 1.4553× | No |
| 5.1 | 8,596.18 (781.38) | 4,479.10 (261.29) | 1.9661× | No |

These numbers are **indicative microbenchmarks in a virtualized environment, not measurements on the upstream reviewer's hardware**. They cannot establish a streaming-system speed-up.

### P3 dispatch

No caller, so measuring branch selection alone has no worthwhile product implication. Do not add Rust FFI overhead solely to invoke a C SSE2 noise-gate kernel. Reconsider only alongside a genuine Rust hot kernel, per-tier forced dispatch, unsupported-tier rejection and identical output.

### P4 bytes, not runtime

1920 × 1080 × 1.5 = **3,110,400 bytes** per NV12 frame; ×60 = **186,624,000 copied bytes/s** on the raw host-plane path. The old prototype's 120 accepted frames recorded zero bytes in this **one host-side copy loop** for the texture route, versus 373,248,000 copied bytes for the raw path. GPU→GPU texture copying and CPU waits remain, so this is not an end-to-end zero-copy claim and says nothing about CPU-ms or latency. Source: [upstream #755](https://github.com/obs-rust/obs-studio/issues/755). Current split heads must be repaired and remeasured.

## Example pipeline: honest partial estimate

1440p60 canvas → 1080p60 NV12 stream. One stereo 48 kHz microphone and one 5.1 48 kHz desktop track. Each uses a compressor, limiter and noise gate. Only **noise-gate** scalar and SSE2 per-block timings are known; compressor/limiter and scaler timings are missing.

- Calls per 1,024-sample audio block per track: `48,000 / 1,024 = 46.875 blocks/s`.
- Stereo noise-gate difference: `7,174.35 - 4,874.94 = 2,299.41 ns/block`.
- 5.1 noise-gate difference: `8,596.18 - 4,479.10 = 4,117.08 ns/block`.
- Together: `(2,299.41 + 4,117.08) × 46.875 / 1,000,000 = 0.30077 CPU-ms saved per second`.
- At 60 fps: `0.30077 / 60 = 0.005013 CPU-ms per frame`.
- Fraction of a 16.6667 ms frame budget: `0.005013 / 16.6667 × 100 = 0.03008%`.

That is a **microbenchmark-derived projection for the two noise-gate instances only**, not a complete pipeline estimate and not an OBS run. Do not extrapolate it to a bigger percentage without measured compressor, limiter and scaler data. The raw copy arithmetic for P4 is separate; P4 may bypass parts of P1's CPU conversion pipeline, so their hypothetical gains cannot simply be added.

## Next proof gates

1. P1: run one reviewer-controlled `--measure` with stable host load and publish raw rounds, machine/flags, medians/IQR and actual pass/fallback hit counts. Do not time on the owner's busy machines.
2. P2: native Rust OFF/ON project test/format receipt for #85. Then independent per-kernel compressor and limiter tests on a clean base, if maintainers want approximate rather than bit-exact output. Reconcile intentional scalar edge correction #91.
3. P3: no standalone merge candidate without a caller; keep the old branch for reference only.
4. P4: resolve pool stop/start race, texture-only raw fallback, exhaustion and evidence double-reservation; then run headless lease/counter tests and real VideoToolbox on the owner's Mac. Keep all timing claims blank until an A/B run.

## Packaging

This is a documentation-only index. RED/GREEN does not apply to this umbrella; its component branches carry those proofs. Authoring tools available in the current environment cannot execute pinned formatters or soldr; local attempts returned exit 127 for clang-format-22, gersemi and soldr cargo fmt/clippy. The umbrella modifies no code, workflow, runner or upstream-connected ref.
