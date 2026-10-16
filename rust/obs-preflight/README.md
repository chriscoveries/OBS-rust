# obs-preflight

`obs-preflight` is the headless M0 engine for OBS preflight decisions. It has
no OBS, GPU, network, or platform dependency. OBS-side code can pass timestamped
counters to this crate later; the CLI replays those counters from JSON Lines.

## Trace and CLI

Each non-empty JSONL line is one `StatSample`. Required fields are:

`timestamp_ms`, `average_frame_time_ns`, `frame_interval_ns`, `lagged_frames`,
`output_total_bytes`, `output_total_frames`, `output_frames_dropped`,
`output_congestion`, `connect_time_ms`, `encoder_total_frames`,
`encoder_skipped_frames`, and `cpu_percent`.

The total-frame counters come from `obs_output_get_total_frames` and
`video_output_get_total_frames`. They make drop and skip percentages exact and
let the engine fail closed when the encoder produces no frames. Congestion is
the OBS fraction from 0.0 to 1.0; CPU is a percentage from 0 to 100.
Counters are cumulative within a run. If any counter decreases after an output
or video restart, that sample starts a new segment and the interval across the
reset is omitted. `roll_up_windows` reports separate windows for each segment;
`roll_up_window` returns only the latest contiguous segment.

Replay the default five-second windows with:

    soldr cargo run -p obs-preflight --bin obs-preflight -- replay trace.jsonl

Choose another window size or read from stdin with:

    soldr cargo run -p obs-preflight --bin obs-preflight -- replay trace.jsonl --window-ms 10000
    cat trace.jsonl | soldr cargo run -p obs-preflight --bin obs-preflight -- replay -

The CLI prints one JSON document with each window's rollup, `pass`, `marginal`,
or `fail` verdict, and a plain-words reason.
Replay fails with a diagnostic if no measurable interval remains after counter
resets, and reports stdout write failures (for example, when a pipe reader exits).

## C ABI and OBS sampler

`frontend/preflight/obs-preflight.h` is the hand-written C ABI, implemented in
`rust/obs-preflight/src/ffi/` and built as a static library by the thin
`obs-preflight-ffi` workspace crate. The engine remains an ordinary Rust
library for the CLI and Tier 1 tests. Its opaque C handle accepts cumulative,
timestamped `obs_preflight_sample` values and returns one
`obs_preflight_window` per populated window, including the verdict and a
NUL-terminated reason. Create a new engine for each run. If output or video
counters reset, the next sample starts a new window and the crossing interval
is omitted. A zero custom window duration is rejected, and window queries
return an analysis error until two valid samples are present.

The OBS-facing sampler that reads stats from OBS and pushes them into the
engine arrives in a follow-up.

## Verdict thresholds

Verdict bands follow warnings OBS already displays: OBS `text-warning` and
warning notifications map to Marginal, and `text-danger` maps to Fail. The
thresholds below come from OBS upstream master `aaed86fdd`.

| Signal | Pass | Marginal | Fail | OBS source |
| --- | --- | --- | --- | --- |
| Average render time / frame interval | At most 0.75× | Above 0.75× through 1× | Above 1× | `frontend/widgets/OBSBasicStats.cpp:364-366` |
| Render lag (lagged / rendered frames) | At most 1% | Above 1% through 5% | Above 5% | `frontend/widgets/OBSBasicStats.cpp:419-421` |
| Output drops (dropped / output frames) | At most 1% | Above 1% through 5% | Above 5% | `frontend/widgets/OBSBasicStats.cpp:579-581` |
| Encoder skips (skipped / encoder frames) | At most 1% | Above 1% through 5% | Above 5% | `frontend/widgets/OBSBasicStats.cpp:393-395` |
| Output congestion fraction | At most 0.3333 | Above 0.3333 through 0.6667 | Above 0.6667 | `frontend/widgets/OBSBasicStatusBar.cpp:10-13, 373-379` |
| Encoder skips: status-bar warning | No warning condition | More than 10 new skips and at least 0.1% | — | `frontend/widgets/OBSBasicStatusBar.cpp:492` |
| Encoder frames | At least one frame | — | Zero frames | Data-validity check: with zero frames, there is nothing to measure. |

OBS compares render time, render lag, output drops, and encoder skips with
strict `>` checks, so equality remains in the lower band. The status-bar
encoder rule is an additional Marginal condition; its percentage is skipped /
total × 100, and both its count and percentage conditions must hold. CPU usage
remains in samples and rollups as information, but OBS shows no CPU warning,
so it does not affect the verdict.

OBS congestion has four icon levels: excellent through 0 (+epsilon), good
through 0.3333, mediocre through 0.6667, and bad through 1.0. The four-to-three
verdict mapping is ours: excellent and good map to Pass, mediocre to Marginal,
and bad to Fail. The rollup reports congestion as a percentage for compatibility;
verdict comparisons use its underlying 0-to-1 fraction.

The server chooser follows `frontend/wizards/AutoConfigTestPage.cpp:393-397`:
when measured bitrate is under 75% of target or any output frame dropped, it
reports 70% of measured bitrate as capacity; otherwise it reports the target.
Rates less than 400 kbps apart are tied and use lower
`obs_output_get_connect_time_ms`, matching lines 414-416. The 75%, 70%, and
400 kbps constants are baseline behavior, not new scoring limits. Each
`ServerTest` must contain samples from one isolated test; the drop total is
run-local and a positive count seen at the trace baseline still triggers the
wizard's 70% estimate.

The bake-off planner preserves candidate input order and assigns the supplied
duration to each candidate. A clear failure can end that candidate's remaining
sample time; the planner records its verdict and continues with the next
candidate. Pass and marginal results continue in the same way.
