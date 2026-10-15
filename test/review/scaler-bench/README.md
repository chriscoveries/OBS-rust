# Paired video-scaler benchmark

Run `bash test/review/scaler-bench/run.sh --quick` first, then `bash test/review/scaler-bench/run.sh --measure` on a quiet macOS or Linux machine. `SCALER_BENCH_FRAMES=60` changes the timed batch length. FFmpeg development headers and pkg-config files are required. The runner can reuse `.deps`, or honour an explicitly supplied `PKG_CONFIG_PATH`. It installs nothing, runs no OBS process and does not dispatch CI.

The executable includes the checkout's actual `video-scaler-ffmpeg.c`. Two instances have identical input, destination geometry and packed allocation layout. Only the reference instance's private `direct_height_ok` is disabled, retaining the existing scratch-and-copy path. A wrapper observes the actual `sws_scale` destination for every invocation; a wrong-path count fails the run. The benchmark supplies only the 32-byte allocator and logger that this source needs, so results measure the scaler path rather than full OBS throughput.

Each format (BGRA, I420, NV12, P010) has same-size 1080p, 4K-to-1080p and 1080p-to-4K cases. Eight paired batches alternate copy/direct and direct/copy ordering. Allocations, input generation, comparison and canary checks are outside timing. Pixels are compared after warmup and every pair. Guard corruption or output inequality stops the executable.

Raw JSONL is retained with HEAD, source blob, worktree state and process-load snapshots. The report includes per-frame medians and the range of paired time differences. These are observations that require host-load review, not a confidence interval or a speed guarantee. Quick mode uses small images and intentionally omits performance results.

Observed in the authoring container: all eight report-validation unit tests passed and shell syntax passed. Native benchmark compilation/execution is pending because the container lacks FFmpeg development headers. No timing result is claimed. Run the normal project tests and formatting before upstream promotion. No production source or public API changes.
