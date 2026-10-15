# Reviewer-run video-scaler A/B benchmark

This standalone harness compares the exact source at the merge-base of upstream draft #743 with its PR head, in one process and against the local system FFmpeg/swscale. It builds only `video-scaler-ffmpeg.c`; it does not build or start OBS. Requires Python 3, CMake 3.28+, a C11 compiler, `pkg-config`, and FFmpeg development packages for `libswscale` and `libavutil`.

Run from the checkout root:

```sh
bash test/review/scaler-bench/run.sh --quick
bash test/review/scaler-bench/run.sh --dry-timing
bash test/review/scaler-bench/run.sh --measure
```

`--quick` checks byte-identical output for all 12 format/geometry cases without reading a clock. `--dry-timing` runs one warm-up and one tiny timing round per case, with each timed batch calibrated to last at least about 10 ms. It prints a prominent `NOT A MEASUREMENT` report and does not sample host load. The only mode that measures full-size 1080p/4K cases is `--measure`. It takes a three-second whole-machine CPU sample before and during the run, warns at more than 15% busy, and requests a quiet-machine resample. `--force` bypasses that prompt. Each case warms both versions, then interleaves before/after order across at least seven rounds. Results include median, minimum, IQR, a paired bootstrap interval for the speed-up ratio, and an inconclusive label when the IQR ranges overlap.

The twelve cases cover BGRA, I420, NV12, and P010 at same-size 1080p, 4K-to-1080p, and 1080p-to-4K. Inputs, output allocation, and pixel comparison are outside the timed batch. Every case is checked byte-for-byte before and after timing. Any mismatch or wrong scaler path aborts the run. Raw per-round values and metadata are saved as JSON beside the Markdown report in `scaler-bench-results/` by default.

The default is correctness-only. Nothing is posted unless a full measurement is run with an explicit fork PR URL:

```sh
bash test/review/scaler-bench/run.sh --measure --post https://github.com/chriscoveries/obsrust-fork/pull/123
```

The poster accepts only `chriscoveries/obsrust-fork` PR URLs and requires `gh auth status` to succeed. No upstream repository is contacted for posting.

The shared adapter contract is: enumerate named cases, provide a correctness check before sampling, expose one timed batch for each source revision, and return raw paired round records (`case`, `round`, `order`, `frames`, `before_ns`, `after_ns`). Machine-load sampling, scheduling, statistics, and Markdown rendering stay in `tools/ab_bench/core.py`. A future dynamics SIMD adapter should supply its two implementation builds and correctness oracle without changing those shared helpers.
