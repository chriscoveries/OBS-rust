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

## M0 thresholds

| Signal | Pass | Marginal | Fail | Basis |
| --- | --- | --- | --- | --- |
| Render headroom | At least 20% of frame interval | Below 20%, but non-negative | Negative | Provisional owner-approved Preflight M0 policy (2026-10-13): a fifth of an interval leaves room for short render bursts; fail when rendering exceeds the interval. OBS defines no verdict band. |
| Render lag | 0% | Above 0% and below 1% | At least 1% | Provisional owner-approved Preflight M0 policy (2026-10-13): any missed render frame is visible; a sustained one-in-a-hundred is a clear failure. |
| Output drops | 0% | Above 0% and below 1% | At least 1% | Provisional owner-approved Preflight M0 policy (2026-10-13): any network drop is marginal; a sustained one-in-a-hundred is a clear failure. The server chooser separately preserves the wizard rule below. |
| Encoder skips | Below 0.1%, or at most 10 new skips | At least 0.1% and more than 10 new skips, but below 1% | At least 1% | OBS's `frontend/widgets/OBSBasicStatusBar.cpp:474-491` calculates `skipped / total * 100.0`, then warns at `>= 0.1` and `diff > 10`. Therefore `0.1` means 0.1%, not 10%. The 1% fail band is provisional owner-approved Preflight M0 policy (2026-10-13): one percent is sustained failure; OBS sets no fail threshold. |
| Output congestion | At most 5% | Above 5% and below 20% | At least 20% | Provisional owner-approved Preflight M0 policy (2026-10-13): small variation is normal and 20% is material capacity loss. OBS reports a 0-to-100% display value but defines no verdict bands. |
| CPU | Below 90% | From 90% to below 98% | At least 98% | Provisional owner-approved Preflight M0 policy (2026-10-13): 90% leaves little room for bursts; 98% leaves almost none. OBS defines no preflight CPU band. |
| Encoder frames | At least one frame | — | Zero frames | A device that emits no frames is unusable for this candidate; fail closed. |

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
