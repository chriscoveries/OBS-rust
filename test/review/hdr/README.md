# HDR recording with adjustable tone mapping

Fork-local owner review. Upstream-facing branches and uploads remain subject to owner approval.

## Current delivery state

The seven native-file edits are still described by the original SHA-pinned `integrate.py`. Their generated source commit has not yet been published. The revised runner automates the remaining conversion: in a clean detached worktree it computes the exact expected output blobs, applies the original transformation unchanged, verifies all seven results, and creates an ordinary local source commit before building. It records that commit and its file hashes. The runner never moves an existing branch, pushes, dispatches CI or uploads a video.

A configured Git author/committer identity is required. Preparation refuses attached HEAD, unrelated changes, staged changes and unexpected source bytes. No commit hooks run and no CI attestation is asserted. Once an integrated commit contains `native-integration.json`, subsequent runs verify those committed sources instead of applying the transformation again.

## Commands

Use a clean checkout of `review/hdr-recording-hlg-20261015`:

```sh
# CPU mathematics, verifier and preparation-helper tests
bash test/review/hdr/run.sh --cpu

# Create and retain an ordinary native source commit, without an OBS build
bash test/review/hdr/run.sh --prepare

# Prepare that commit, then build and validate both native configurations
bash test/review/hdr/run.sh --native
```

`--native` requires a configured Mac with full Xcode, soldr, CMake and cmocka. Read the repository CI-tooling guide and respect shared-machine build gates. Logs include the review HEAD, generated native commit, patch, configuration, builds and CTest results. The detached worktree is retained. After inspecting its results, the owner can promote the native source commit to a review branch.

## Mac runner repairs

The runner now builds `test_virtual_cam_shutdown` and `test_config_migration` in addition to the full app, encoder, muxer and cmocka targets. A local helper enumerates registered CTest executables, adds missing runtime paths including each executable's own directory, and signs those test executables ad hoc with library-validation exceptions for Homebrew cmocka. It verifies the signatures and records every touched file. The OBS application and plugins are excluded; system signing settings are unchanged.

These are the setup repairs identified in the Mac agent's prior run. Their automation still needs a native rerun. Rust OFF/ON exported-symbol parity remains mandatory.

## Recording controls

Select Settings > Output > Advanced > Recording, a separate Apple HEVC recording encoder and Hybrid MP4. Enable HDR recording (10-bit HLG). Leave the streaming encoder's HDR checkbox off. The properties are saved in the recording encoder's settings JSON.

| Control | Default | Range |
|---|---:|---|
| Exposure | 0 EV | -4 to +4 EV |
| Reference white | 203 nits | 80 to 400 nits |
| Target peak | 1,000 reference nits | 400 to 1,000, above reference white |
| Highlight roll-off | Soft shoulder | Soft shoulder or hard limit |
| Compression begins | 80% of peak | 50 to 95% |

Stop recording before changing the grade. No live grade preview or waveform UI is included. Values are authored starting points, not display calibration.

The output target is HEVC Main 10, P010 10-bit 4:2:0, limited-range BT.2020/HLG in hvc1 MP4, at most 60 fps, with one AAC mono/stereo audio track for the upload copy. This is a compatibility target that requires actual recording and device tests.

## Processing contract

A private immutable recording profile distinguishes differently graded encoder mixes. Both mix-reuse checks include every tone setting, and shared conversion-effect uniforms reset for every mix. P010 HLG pixels are graded before encoding. Failed encoder initialization releases its recording mix; incompatible format/range/frame rate is rejected.

Input to the mapper is display-linear BT.2020 RGB with 1 representing 10,000 nits. Exposure multiplies by 2^EV. Negative components clamp to zero. A common positive gain scales RGB components from the maximum component L. For peak P, knee K and D=P-K, the soft shoulder uses L below K and P-D/(1+(L-K)/D) above it. This preserves continuity and unit slope at the knee and asymptotically approaches P. Hard limit uses min(L,P). It is an authored curve, not a standardized mastering operator.

The peak cap avoids the existing HLG converter's separate above-1,000-nit mapping. Reference white scales the scene; exposure cannot recover information already clipped at capture. The existing hvc1/hvcC/colr metadata is retained, with an authored 314 lux/D65 ambient reference. Slider values are not presented as measured mastering-display or content-light metadata. The older optional codec-specific metadata experiment stays disabled.

## Evidence and remaining gates

The original patched tree at review head `afa7e7675` has a Mac-agent receipt on this PR: full app/plugins built OFF/ON, both 141-test CTest suites passed with the same expected SVT-AV1 skip, and 1,878 exported symbols matched. That receipt used manual runner repairs and does not cover this revised preparation/runtime automation or its eventual generated source commit.

The new preparation/runtime selection helpers passed nine local tests in disposable repositories. They verify owner-branch preservation, exact source manifests, refusal of unrelated changes and selection of registered test executables. Bash syntax passes. Local helper tests do not exercise Apple signing tools or a complete OBS build.

Run the new native command, preserve the ordinary source commit and repeat all build/test gates. Then launch the built application, verify shader compilation and record a real HDR acceptance scene. Check changed exposure/shoulders, SDR overlay readability, repeated start/stop/failure cleanup, differently graded encoders and simultaneous SDR output. Exercise Metal and OpenGL separately where supported.

```sh
uv run test/review/hdr/verify_recording.py /absolute/path/recording.mp4 --report hdr-file-report.json
```

The checker compares ffprobe fields, HEVC SPS/VUI and MP4 colour/ambient boxes, then decodes sample frames. Check complete-file integrity and late frames separately for long recordings.

Transfer the original file to a compatible iPhone and test Photos HDR playback. With separate upload approval, upload the same original through native Instagram and Facebook apps, wait for their processing and inspect HDR and SDR renditions. Record device, OS, app, account, display settings and upload route. API/desktop routes need separate tests. File validity alone does not establish that either platform will deliver HDR for a particular playback session.

## Primary references

- Meta HDR Reels processing: https://engineering.fb.com/2023/07/17/video-engineering/hdr-video-reels-meta/
- Apple HDR metadata: https://developer.apple.com/documentation/technotes/tn3145-hdr-video-metadata
- Apple HLG authoring guidance (HLS scope): https://developer.apple.com/streaming/hls-authoring-specification-for-apple-devices.html
- FFmpeg amve units: https://ffmpeg.org/doxygen/trunk/mov_8c_source.html

See VALIDATION.md for the separate local and Mac-reported receipts. Neither iPhone playback nor Meta upload acceptance has been completed.
