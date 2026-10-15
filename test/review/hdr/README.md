# HDR recording with adjustable tone mapping

Owner-review implementation handoff, based on `72c48c4ecebacfb0e7da6b8a8dbc7cee97aef258`.
No upstream-facing branch is changed. Do not merge, post upstream, or upload a video without owner approval.

## Delivery form

New C helpers, shader, property controls and tests are source files in this branch. `integrate.py` contains the native edits to seven existing files. It checks their exact Git blob IDs, emits a normal unified patch, and applies it only in a clean detached worktree. `run.sh --native` performs that step automatically.

**The native patch has not been applied to a complete checkout here.** Before an upstream PR is prepared, the Mac agent must run it, retain the resulting patch, and flatten the verified native edits into ordinary commits on a new owner-review branch. This is an implementation candidate awaiting native validation.

## Recording controls

In Settings > Output > Advanced > Recording, select a **separate Apple HEVC recording encoder** and **Hybrid MP4**, then enable **HDR recording (10-bit HLG)**. Use a single mixed AAC audio track for the upload copy. The generic encoder properties are per encoder; leave the streaming encoder's HDR checkbox off. Controls use the existing recording-encoder settings JSON.

| Control | Default | Range / behaviour |
|---|---:|---|
| HDR recording | Off | Isolated P010, limited-range BT.2100 HLG encoder mix |
| Exposure | 0 EV | -4 to +4 EV in linear light |
| Reference white | 203 nits | 80 to 400 nits; scales recorded brightness |
| Target peak brightness | 1,000 nits | 400 to 1,000 reference nits, above reference white |
| Highlight roll-off | Soft shoulder | Smooth compression or a hard peak limit |
| Compression starts at | 80% | 50 to 95% of peak for the soft shoulder |

These are authored starting values, not a calibration of the viewer's display. Stop recording to change them. This draft has no live recording-grade preview or waveform UI.

## Native integration

- Both encoder-mix reuse checks include the complete tone profile. Global video settings and existing output defaults remain unchanged.
- P010 HLG pixels are graded in the GPU conversion shader before encoding. Shared effect uniforms are reset on every mix, including the SDR mix.
- Apple HEVC is forced to Main 10; incompatible format/range or fps > 60 fails initialization.
- Failed encoder creation releases its encoder-only mix. Tone changes during an active encoder are rejected.
- The existing MP4 `hvc1`/`hvcC`/`colr` path carries the encoder's HLG colour information. The new `amve` box authors a 314 lux / D65 reference environment.
- Inferred mastering-display/content-light measurements are omitted for this mode. The existing optional codec-specific metadata experiment stays disabled.

Ambient units are 1/10000 lux and 1/50000 chromaticity. The reference is authored, not measured scene brightness or a measurement of the viewer's room.

## Curve

Input is display-linear BT.2020 RGB normalized so 1 = 10,000 nits. Exposure is `2^EV`. Negative components clamp to zero, then one positive gain scales all components. Let `L=max(R,G,B)`, `P=peak`, `K=P*knee_fraction`, `D=P-K`:

```
L <= K: output_level = L
L > K:  output_level = P - D / (1 + (L-K)/D)
```

The shoulder is continuous, has unit derivative at the knee, is monotonic and approaches the peak. Hard limit uses `min(L,P)`. This is an authored low-cost curve, not a claim to implement a standardized mastering operator.

The existing `linear_to_hlg` uses a fixed 1,000-nit reference for Lw <= 1,000. Capping this preset's peak at 1,000 avoids its separate >1,000-nit down-map being applied on top of the custom curve. Actual display luminance remains adaptive.

Reference white scales the whole recorded scene. Genuine HDR source information can be retained; an SDR source contains only its original information. Exposure cannot recover clipped source pixels.

## Commands

From this review branch, CPU checks:

```sh
bash test/review/hdr/run.sh --cpu
```

On the configured Mac, with full Xcode, cmake, cmocka and soldr:

```sh
bash test/review/hdr/run.sh --native
```

The runner creates a detached worktree, applies the pinned native patch, builds the frontend/plugins with Rust OFF/ON, runs C tests and compares exports. It retains the patch, logs and worktree. No commits, pushes, CI dispatches or uploads occur. Read `docs/ci-tooling.md` and respect shared-machine gates first.

Validate an actual completed recording:

```sh
uv run test/review/hdr/verify_recording.py /absolute/path/recording.mp4 --report hdr-file-report.json
```

This checks ffprobe fields, HEVC SPS/VUI, MP4 colour/ambient boxes, and 30 decoded frames. A headline HDR tag cannot hide disagreement between container and bitstream.

## Completed checks

Host: x86-64 Linux, Clang 17, FFmpeg 7.1.5; no Mac SDK or running OBS.

- C contract: **1,020,102** curve/RGB cases plus bounds, bypass and mix-key tests passed under ASan/UBSan and optimized NDEBUG. The final grid has 17 peaks from 400 to 1,000.
- Exact effect-function source compiled as C++ arithmetic: **360,000** comparisons passed under sanitizers and optimized build. Tolerance: 0.001 nits + 2e-6 relative. This is not an OBS shader-compiler/GPU test.
- Seven file-verifier unit tests, with subcases for all required tags, bitstream disagreement, missing ambient data and malformed containers.
- An independently encoded one-second libx265 HLG/hvc1 fixture passed with `--allow-missing-ambient`. An earlier fixture with conflicting container tags was rejected. This is a verifier fixture, not output from this OBS implementation. Reproduce with `make-verifier-fixture.sh`.
- Shell/Python syntax checked. An identity/no-op mapper compiled then failed the output-bound assertion at the test-first checkpoint.

Native patch application, property/API tests, full app builds, Qt GUI, GPU shader execution, actual OBS HEVC recording, native OFF/ON ABI parity, iPhone playback and Meta uploads remain **NOT TESTED**. See `VALIDATION.md`.

## Acceptance gates

1. Retain the native diff, run formatting/lint, and pass the native property/API test and full OFF/ON suites with identical exports. Launch the built app so its actual bundled shader compiles.
2. Record a genuine HDR source with a known highlight ramp and SDR overlay. Compare 0/+1 EV and different shoulders. Check decoded pixels follow the curve and overlays stay readable. Verify the recording grade does not change a simultaneous SDR stream.
3. Exercise stop/start, enable/disable, failed initialization, two differently graded encoders and repeated starts without leaked mixes. Check Metal and OpenGL separately where supported. The GUI shows controls through the selected encoder's property panel.
4. Run the file validator on the actual Hybrid MP4. Inspect late frames and complete-file integrity separately for long recordings.
5. Transfer the original file without an SDR export to a compatible iPhone. Verify Photos HDR playback and appearance; record model, iOS version, display/accessibility settings, brightness and power state.
6. With explicit upload approval, upload that same original separately through native Instagram and Facebook apps with highest-quality uploads enabled. After processing, check HDR playback on the target iPhone. Record app version, account and route. API/desktop uploads require separate tests; also check each app's SDR rendition.

A valid HLG file cannot force HDR delivery to every device/account. Local tone controls do not select Meta's server-side SDR tone-map operator. The feature is accepted only after the actual recordings pass the device/upload gates.

## Primary references

- Meta HDR Reels upload/pipeline: https://engineering.fb.com/2023/07/17/video-engineering/hdr-video-reels-meta/
- Apple HDR metadata and ambient environment: https://developer.apple.com/documentation/technotes/tn3145-hdr-video-metadata
- Apple HLG/HEVC playback authoring (HLS scope): https://developer.apple.com/streaming/hls-authoring-specification-for-apple-devices.html
- FFmpeg amve units, `mov_read_amve`: https://ffmpeg.org/doxygen/trunk/mov_8c_source.html

These inform the chosen target profile; they do not certify this native implementation or every upload route.
