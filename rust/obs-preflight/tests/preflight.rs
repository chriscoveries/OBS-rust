use obs_preflight::{
    AnalysisError, BakeoffPlanner, Candidate, CandidateMode, DEFAULT_WINDOW_DURATION_MS,
    ENCODER_SKIP_UI_WARNING_PERCENT, FAIL_CONGESTION_FRACTION, FAIL_ENCODER_SKIP_PERCENT,
    FAIL_OUTPUT_DROP_PERCENT, FAIL_RENDER_FRAME_TIME_RATIO, FAIL_RENDER_LAG_PERCENT,
    MARGINAL_CONGESTION_FRACTION, MARGINAL_ENCODER_SKIP_PERCENT, MARGINAL_OUTPUT_DROP_PERCENT,
    MARGINAL_RENDER_FRAME_TIME_RATIO, MARGINAL_RENDER_LAG_PERCENT,
    MIN_ENCODER_SKIPS_FOR_UI_WARNING, PlannerError, SERVER_BITRATE_TIE_KBPS,
    SERVER_CAPACITY_PERCENT, SERVER_MINIMUM_TARGET_PERCENT, ServerTest, StatSample, Verdict,
    choose_server, expand_candidate_matrix, judge_window, roll_up_window, roll_up_windows,
};
use proptest::prelude::*;
use serde::Deserialize;
use std::collections::BTreeMap;
use std::io::Write;
use std::process::{Command, Stdio};

const CLEAN: &str = include_str!("golden/clean.jsonl");
const CONGESTED: &str = include_str!("golden/congested_uplink.jsonl");
const ENCODER_BEHIND: &str = include_str!("golden/encoder_falling_behind.jsonl");
const STALE_ENCODER: &str = include_str!("golden/stale_device_encoder.jsonl");
const SERVER_TIE: &str = include_str!("golden/server_tie.jsonl");

fn parse_samples(trace: &str) -> Vec<StatSample> {
    trace
        .lines()
        .filter(|line| !line.trim().is_empty())
        .map(|line| serde_json::from_str(line).expect("valid stat sample"))
        .collect()
}

#[derive(Deserialize)]
struct ServerRecord {
    server_name: String,
    server_address: String,
    #[serde(flatten)]
    sample: StatSample,
}

fn parse_server_tests(trace: &str) -> Vec<ServerTest> {
    let mut tests = BTreeMap::<String, ServerTest>::new();
    for line in trace.lines().filter(|line| !line.trim().is_empty()) {
        let record: ServerRecord = serde_json::from_str(line).expect("valid server sample");
        tests
            .entry(record.server_name.clone())
            .or_insert_with(|| ServerTest {
                name: record.server_name.clone(),
                address: record.server_address,
                samples: Vec::new(),
            })
            .samples
            .push(record.sample);
    }
    tests.into_values().collect()
}

fn jsonl_for_samples(samples: &[StatSample]) -> String {
    samples
        .iter()
        .map(|sample| serde_json::to_string(sample).expect("serialize sample"))
        .collect::<Vec<_>>()
        .join("\n")
}

fn run_cli_with_trace(trace: &str) -> std::process::Output {
    let mut child = Command::new(env!("CARGO_BIN_EXE_obs-preflight"))
        .args(["replay", "-", "--window-ms", "5000"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("start CLI");
    child
        .stdin
        .take()
        .expect("stdin")
        .write_all(trace.as_bytes())
        .expect("write trace");
    child.wait_with_output().expect("wait for CLI")
}

fn invalid_prefix_after_reset(kind: u8) -> (Vec<StatSample>, AnalysisError) {
    let mut first = parse_samples(CLEAN)[0].clone();
    first.timestamp_ms = if kind == 3 { 3000 } else { 0 };
    first.lagged_frames = 10;
    first.output_total_bytes = 10;
    first.output_total_frames = 10;
    first.output_frames_dropped = 0;
    first.encoder_total_frames = 10;
    first.encoder_skipped_frames = 0;

    let mut reset = first.clone();
    reset.timestamp_ms = 1000;
    reset.lagged_frames = 0;
    reset.output_total_bytes = 0;
    reset.output_total_frames = 0;
    reset.output_frames_dropped = 0;
    reset.encoder_total_frames = 0;
    reset.encoder_skipped_frames = 0;

    let mut after_reset = reset.clone();
    after_reset.timestamp_ms = 2000;
    after_reset.output_total_bytes = 625_000;
    after_reset.output_total_frames = 1000;
    after_reset.encoder_total_frames = 1000;

    let expected = match kind {
        0 => {
            first.frame_interval_ns = 0;
            AnalysisError::InvalidSample {
                sample_index: 0,
                field: "frame_interval_ns",
            }
        }
        1 => {
            first.output_congestion = 2.0;
            AnalysisError::InvalidSample {
                sample_index: 0,
                field: "output_congestion",
            }
        }
        2 => {
            first.cpu_percent = f64::NAN;
            AnalysisError::InvalidSample {
                sample_index: 0,
                field: "cpu_percent",
            }
        }
        3 => AnalysisError::NonIncreasingTimestamp { sample_index: 1 },
        _ => unreachable!("test generates only supported invalid sample kinds"),
    };

    (vec![first, reset, after_reset], expected)
}

// Keep the synthetic counter arguments in the same order as the sample model;
// this makes the threshold boundary cases compact and easy to compare.
#[allow(clippy::too_many_arguments)]
fn two_samples(
    frame_time_ns: u64,
    frame_interval_ns: u64,
    lagged_frames: u32,
    output_frames: u64,
    dropped_frames: u64,
    congestion: f64,
    encoder_frames: u64,
    skipped_frames: u64,
    cpu_percent: f64,
) -> Vec<StatSample> {
    let sample = |timestamp_ms,
                  lagged_frames,
                  output_frames,
                  dropped_frames,
                  encoder_frames,
                  skipped_frames| {
        StatSample {
            timestamp_ms,
            average_frame_time_ns: frame_time_ns,
            frame_interval_ns,
            lagged_frames,
            output_total_bytes: 0,
            output_total_frames: output_frames,
            output_frames_dropped: dropped_frames,
            output_congestion: congestion,
            connect_time_ms: 25,
            encoder_total_frames: encoder_frames,
            encoder_skipped_frames: skipped_frames,
            cpu_percent,
        }
    };
    vec![
        sample(0, 0, 0, 0, 0, 0),
        sample(
            1000,
            lagged_frames,
            output_frames,
            dropped_frames,
            encoder_frames,
            skipped_frames,
        ),
    ]
}

fn verdict_for(samples: &[StatSample]) -> Verdict {
    judge_window(roll_up_window(samples).expect("valid rollup")).verdict
}

const PRESSURE_BAND_EXPECTATIONS: [[Verdict; 3]; 5] = [
    [Verdict::Pass, Verdict::Marginal, Verdict::Fail],
    [Verdict::Pass, Verdict::Marginal, Verdict::Fail],
    [Verdict::Pass, Verdict::Marginal, Verdict::Fail],
    [Verdict::Pass, Verdict::Marginal, Verdict::Fail],
    [Verdict::Pass, Verdict::Marginal, Verdict::Fail],
];

// Build raw cumulative OBS samples at the edge of each expected verdict band.
// Output, lag, and encoder percentages are derived from matching frame counts;
// only the selected pressure signal changes from the passing defaults.
fn coherent_samples_at_pressure(signal: u8, level: u8) -> Vec<StatSample> {
    let mut frame_time_ns = 50_000;
    let mut frame_interval_ns = 100_000;
    let mut lagged_frames = 0;
    let output_frames = 10_000;
    let mut dropped_frames = 0;
    let mut congestion = 0.0;
    let mut encoder_frames = 10_000;
    let mut encoder_skipped_frames = 0;

    match (signal, level) {
        (0, 0..=2) => {
            frame_interval_ns = 1_000;
            frame_time_ns = match level {
                0 => 500,
                1 => 800,
                2 => 1_001,
                _ => unreachable!(),
            };
        }
        (1, 0..=2) => {
            lagged_frames = match level {
                0 => 0,
                1 => 200,
                2 => 501,
                _ => unreachable!(),
            };
        }
        (2, 0..=2) => {
            dropped_frames = match level {
                0 => 0,
                1 => 200,
                2 => 501,
                _ => unreachable!(),
            };
        }
        (3, 0..=2) => {
            let (total_frames, skipped_frames) = match level {
                0 => (10_000, MIN_ENCODER_SKIPS_FOR_UI_WARNING),
                1 => (10_000, 200),
                2 => (10_000, 501),
                _ => unreachable!(),
            };
            encoder_frames = total_frames;
            encoder_skipped_frames = skipped_frames;
        }
        (4, 0..=2) => {
            congestion = match level {
                0 => 0.1,
                1 => 0.5,
                2 => 0.7,
                _ => unreachable!(),
            };
        }
        _ => unreachable!("generated signal and pressure level are in range"),
    }

    two_samples(
        frame_time_ns,
        frame_interval_ns,
        lagged_frames,
        output_frames,
        dropped_frames,
        congestion,
        encoder_frames,
        encoder_skipped_frames,
        50.0,
    )
}

fn verdict_rank(verdict: Verdict) -> u8 {
    match verdict {
        Verdict::Pass => 0,
        Verdict::Marginal => 1,
        Verdict::Fail => 2,
    }
}

#[test]
fn golden_clean_run_passes_with_exact_rollup() {
    let samples = parse_samples(CLEAN);
    let windows = roll_up_windows(&samples, DEFAULT_WINDOW_DURATION_MS).expect("valid trace");
    assert_eq!(windows.len(), 1);
    let result = judge_window(windows.into_iter().next().expect("one window"));
    assert_eq!(result.verdict, Verdict::Pass);
    assert_eq!(
        result.reason,
        "The run kept render headroom and frame loss within the target limits."
    );
    assert_eq!(result.rollup.output_bitrate_kbps, 5000.0);
    assert_eq!(result.rollup.output_drop_percent, 0.0);
    assert_eq!(result.rollup.encoder_skip_percent, 0.0);
    assert_eq!(result.rollup.render_headroom_ns, 8_666_667);
}

#[test]
fn trace_is_split_into_non_overlapping_time_windows() {
    let samples = parse_samples(CLEAN);
    let windows = roll_up_windows(&samples, 2000).expect("valid trace");
    assert_eq!(windows.len(), 3);
    assert_eq!((windows[0].start_ms, windows[0].end_ms), (0, 2000));
    assert_eq!((windows[1].start_ms, windows[1].end_ms), (2000, 4000));
    assert_eq!((windows[2].start_ms, windows[2].end_ms), (4000, 5000));
    assert_eq!(windows[0].output_bitrate_kbps, 5000.0);
    assert_eq!(windows[1].output_bitrate_kbps, 5000.0);
    assert_eq!(windows[2].output_bitrate_kbps, 5000.0);
}

#[test]
fn golden_congested_uplink_is_marginal_with_exact_reason() {
    let result = judge_window(roll_up_window(&parse_samples(CONGESTED)).expect("valid trace"));
    assert_eq!(result.verdict, Verdict::Marginal);
    assert_eq!(
        result.reason,
        "The output dropped frames during this window. Network congestion exceeded OBS's good level (33.33%)."
    );
}

#[test]
fn golden_encoder_falling_behind_is_marginal_with_exact_reason() {
    let result = judge_window(roll_up_window(&parse_samples(ENCODER_BEHIND)).expect("valid trace"));
    assert_eq!(result.verdict, Verdict::Marginal);
    assert_eq!(
        result.reason,
        "The encoder skipped 5.00% of its frames, above OBS's warning level."
    );
}

#[test]
fn golden_stale_device_with_no_frames_fails_closed() {
    let result = judge_window(roll_up_window(&parse_samples(STALE_ENCODER)).expect("valid trace"));
    assert_eq!(result.verdict, Verdict::Fail);
    assert_eq!(
        result.reason,
        "The encoder produced no frames during this window."
    );
}

#[test]
fn golden_server_tie_uses_lower_connection_time_within_400_kbps() {
    let choice = choose_server(&parse_server_tests(SERVER_TIE), 5000).expect("server selected");
    assert_eq!(choice.name, "east");
    assert_eq!(choice.address, "rtmp://east");
    assert_eq!(choice.measured_bitrate_kbps, 7000);
    assert_eq!(choice.ideal_bitrate_kbps, 4900);
    assert_eq!(choice.connect_time_ms, 20);
}

#[test]
fn cli_replays_jsonl_and_emits_verdicts_as_json() {
    let mut child = Command::new(env!("CARGO_BIN_EXE_obs-preflight"))
        .args(["replay", "-", "--window-ms", "5000"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .expect("start CLI");
    child
        .stdin
        .take()
        .expect("stdin")
        .write_all(CLEAN.as_bytes())
        .expect("write trace");
    let output = child.wait_with_output().expect("wait for CLI");
    assert!(output.status.success());
    let result: serde_json::Value = serde_json::from_slice(&output.stdout).expect("JSON output");
    let root_keys: std::collections::BTreeSet<_> = result
        .as_object()
        .expect("root object")
        .keys()
        .map(String::as_str)
        .collect();
    assert_eq!(root_keys, ["windows"].into_iter().collect());
    let window = &result["windows"][0];
    let window_keys: std::collections::BTreeSet<_> = window
        .as_object()
        .expect("window object")
        .keys()
        .map(String::as_str)
        .collect();
    assert_eq!(
        window_keys,
        ["reason", "rollup", "verdict"].into_iter().collect()
    );
    let rollup_keys: std::collections::BTreeSet<_> = window["rollup"]
        .as_object()
        .expect("rollup object")
        .keys()
        .map(String::as_str)
        .collect();
    assert_eq!(
        rollup_keys,
        [
            "average_frame_time_ns",
            "connect_time_ms",
            "congestion_percent",
            "cpu_percent",
            "duration_ms",
            "encoder_frames",
            "encoder_frames_skipped",
            "encoder_skip_percent",
            "end_ms",
            "frame_interval_ns",
            "lagged_frames",
            "output_bitrate_kbps",
            "output_bytes",
            "output_drop_percent",
            "output_frames",
            "output_frames_dropped",
            "render_headroom_ns",
            "render_lag_percent",
            "start_ms",
        ]
        .into_iter()
        .collect()
    );
    assert_eq!(result["windows"][0]["verdict"], "pass");
    assert_eq!(
        result["windows"][0]["reason"],
        "The run kept render headroom and frame loss within the target limits."
    );
    assert_eq!(
        result["windows"][0]["rollup"]["output_bitrate_kbps"],
        5000.0
    );
}

#[test]
fn cli_rejects_invalid_window_and_malformed_json_without_json_output() {
    let mut invalid_window = Command::new(env!("CARGO_BIN_EXE_obs-preflight"))
        .args(["replay", "-", "--window-ms", "0"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("start CLI");
    invalid_window
        .stdin
        .take()
        .expect("stdin")
        .write_all(CLEAN.as_bytes())
        .expect("write trace");
    let output = invalid_window.wait_with_output().expect("wait for CLI");
    assert!(!output.status.success());
    assert!(output.stdout.is_empty());
    assert!(
        String::from_utf8_lossy(&output.stderr).contains("--window-ms must be greater than zero")
    );

    let mut malformed = Command::new(env!("CARGO_BIN_EXE_obs-preflight"))
        .args(["replay", "-"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("start CLI");
    malformed
        .stdin
        .take()
        .expect("stdin")
        .write_all(b"{not json}\n")
        .expect("write malformed trace");
    let output = malformed.wait_with_output().expect("wait for CLI");
    assert!(!output.status.success());
    assert!(output.stdout.is_empty());
    assert!(String::from_utf8_lossy(&output.stderr).contains("stdin:1:"));
}

#[test]
fn cli_rejects_reset_only_and_repeated_reset_traces() {
    let mut first = parse_samples(CLEAN)[0].clone();
    first.timestamp_ms = 0;
    first.lagged_frames = 10;
    first.output_total_bytes = 0;
    first.output_total_frames = 0;
    first.output_frames_dropped = 0;
    first.encoder_total_frames = 0;
    first.encoder_skipped_frames = 0;
    let mut reset_only = first.clone();
    reset_only.timestamp_ms = 1000;
    reset_only.lagged_frames = 0;

    let mut repeated = vec![first.clone()];
    let mut lag_reset = first.clone();
    lag_reset.timestamp_ms = 1000;
    lag_reset.lagged_frames = 0;
    lag_reset.output_total_bytes = 1;
    repeated.push(lag_reset.clone());
    let mut byte_reset = lag_reset;
    byte_reset.timestamp_ms = 2000;
    byte_reset.lagged_frames = 1;
    byte_reset.output_total_bytes = 0;
    repeated.push(byte_reset);
    let mut another_lag_reset = first.clone();
    another_lag_reset.timestamp_ms = 3000;
    another_lag_reset.lagged_frames = 0;
    another_lag_reset.output_total_bytes = 1;
    repeated.push(another_lag_reset);

    for samples in [vec![first.clone(), reset_only], repeated] {
        let output = run_cli_with_trace(&jsonl_for_samples(&samples));
        assert!(!output.status.success());
        assert!(output.stdout.is_empty());
        assert!(
            String::from_utf8_lossy(&output.stderr)
                .contains("no measurable sample interval after counter resets")
        );
    }
}

#[test]
fn cli_returns_a_stdout_error_when_the_consumer_closes_early() {
    let mut child = Command::new(env!("CARGO_BIN_EXE_obs-preflight"))
        .args(["replay", "-"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("start CLI");
    drop(child.stdout.take());
    child
        .stdin
        .take()
        .expect("stdin")
        .write_all(CLEAN.as_bytes())
        .expect("write trace");
    let output = child.wait_with_output().expect("wait for CLI");
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(!output.status.success());
    assert!(stderr.contains("obs-preflight:"), "{stderr}");
    assert!(!stderr.contains("panicked"), "{stderr}");
}

#[test]
fn render_time_threshold_boundaries_are_exact() {
    let interval = 1000;
    assert_eq!(MARGINAL_RENDER_FRAME_TIME_RATIO, 0.75);
    assert_eq!(FAIL_RENDER_FRAME_TIME_RATIO, 1.0);
    assert_eq!(
        verdict_for(&two_samples(750, interval, 0, 1000, 0, 0.0, 1000, 0, 50.0)),
        Verdict::Pass
    );
    assert_eq!(
        verdict_for(&two_samples(751, interval, 0, 1000, 0, 0.0, 1000, 0, 50.0)),
        Verdict::Marginal
    );
    assert_eq!(
        verdict_for(&two_samples(1000, interval, 0, 1000, 0, 0.0, 1000, 0, 50.0)),
        Verdict::Marginal
    );
    assert_eq!(
        verdict_for(&two_samples(1001, interval, 0, 1000, 0, 0.0, 1000, 0, 50.0)),
        Verdict::Fail
    );
}

#[test]
fn render_lag_threshold_boundaries_are_exact() {
    let no_lag = two_samples(50_000, 100_000, 0, 10_000, 0, 0.0, 10_000, 0, 50.0);
    assert_eq!(verdict_for(&no_lag), Verdict::Pass);

    assert_eq!(MARGINAL_RENDER_LAG_PERCENT, 1.0);
    assert_eq!(FAIL_RENDER_LAG_PERCENT, 5.0);
    for (lagged, expected) in [
        (100, Verdict::Pass),
        (101, Verdict::Marginal),
        (500, Verdict::Marginal),
        (501, Verdict::Fail),
    ] {
        assert_eq!(
            verdict_for(&two_samples(
                50_000, 100_000, lagged, 10_000, 0, 0.0, 10_000, 0, 50.0
            )),
            expected
        );
    }
}

#[test]
fn output_drop_threshold_boundaries_are_exact() {
    let no_drops = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 1000, 0, 50.0);
    assert_eq!(verdict_for(&no_drops), Verdict::Pass);

    assert_eq!(MARGINAL_OUTPUT_DROP_PERCENT, 1.0);
    assert_eq!(FAIL_OUTPUT_DROP_PERCENT, 5.0);
    for (dropped, expected) in [
        (100, Verdict::Pass),
        (101, Verdict::Marginal),
        (500, Verdict::Marginal),
        (501, Verdict::Fail),
    ] {
        assert_eq!(
            verdict_for(&two_samples(
                500_000, 1_000_000, 0, 10_000, dropped, 0.0, 1000, 0, 50.0
            )),
            expected
        );
    }
}

#[test]
fn encoder_skip_ui_warning_boundaries_are_exact() {
    let count_gate_not_met = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 2_000, 10, 50.0);
    assert_eq!(verdict_for(&count_gate_not_met), Verdict::Pass);
    assert_eq!(MIN_ENCODER_SKIPS_FOR_UI_WARNING, 10);
    assert_eq!(ENCODER_SKIP_UI_WARNING_PERCENT, 0.1);
    let exact_ui_warning = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 11_000, 11, 50.0);
    assert_eq!(verdict_for(&exact_ui_warning), Verdict::Marginal);
    assert_eq!(MARGINAL_ENCODER_SKIP_PERCENT, 1.0);
    assert_eq!(FAIL_ENCODER_SKIP_PERCENT, 5.0);
    for (skipped, expected) in [
        (10, Verdict::Pass),
        (11, Verdict::Marginal),
        (50, Verdict::Marginal),
        (51, Verdict::Fail),
    ] {
        assert_eq!(
            verdict_for(&two_samples(
                500_000, 1_000_000, 0, 1000, 0, 0.0, 1_000, skipped, 50.0
            )),
            expected
        );
    }
}

#[test]
fn encoder_status_bar_warning_requires_both_count_and_percent() {
    let eleven_at_point_one_percent =
        two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 11_000, 11, 50.0);
    assert_eq!(verdict_for(&eleven_at_point_one_percent), Verdict::Marginal);

    let ten_at_half_percent = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 2_000, 10, 50.0);
    assert_eq!(verdict_for(&ten_at_half_percent), Verdict::Pass);
}

#[test]
fn congestion_threshold_boundaries_are_exact() {
    assert_eq!(MARGINAL_CONGESTION_FRACTION, 0.3333);
    assert_eq!(FAIL_CONGESTION_FRACTION, 0.6667);
    for (fraction, expected) in [
        (0.3333, Verdict::Pass),
        (0.3334, Verdict::Marginal),
        (0.6667, Verdict::Marginal),
        (0.6668, Verdict::Fail),
    ] {
        assert_eq!(
            verdict_for(&two_samples(
                500_000, 1_000_000, 0, 1000, 0, fraction, 1000, 0, 50.0
            )),
            expected
        );
    }
}

#[test]
fn high_cpu_is_informational_only() {
    assert_eq!(
        verdict_for(&two_samples(
            500_000, 1_000_000, 0, 1000, 0, 0.0, 1000, 0, 99.9
        )),
        Verdict::Pass
    );
}

fn server_test(name: &str, bitrate_kbps: u32, dropped: u64, connect_time_ms: i32) -> ServerTest {
    let bytes = u64::from(bitrate_kbps) * 10_000 / 8;
    let mut samples = two_samples(8_000_000, 16_666_667, 0, 1000, dropped, 0.0, 1000, 0, 50.0);
    samples[1].timestamp_ms = 10_000;
    samples[1].output_total_bytes = bytes;
    samples[0].connect_time_ms = connect_time_ms;
    samples[1].connect_time_ms = connect_time_ms;
    ServerTest {
        name: name.into(),
        address: format!("rtmp://{name}"),
        samples,
    }
}

fn server_test_with_reset(
    name: &str,
    bitrate_kbps: u32,
    dropped: u64,
    connect_time_ms: i32,
    reset_after_initial_window: bool,
) -> ServerTest {
    let mut test = server_test(name, bitrate_kbps, dropped, connect_time_ms);
    if reset_after_initial_window {
        let mut reset = test.samples[1].clone();
        reset.timestamp_ms = 11_000;
        reset.lagged_frames = 0;
        reset.output_total_bytes = 0;
        reset.output_total_frames = 0;
        reset.output_frames_dropped = 0;
        reset.encoder_total_frames = 0;
        reset.encoder_skipped_frames = 0;

        let mut after_reset = reset.clone();
        after_reset.timestamp_ms = 21_000;
        after_reset.output_total_bytes = u64::from(bitrate_kbps) * 1250;
        after_reset.output_total_frames = 1000;
        after_reset.output_frames_dropped = dropped;
        after_reset.encoder_total_frames = 1000;
        test.samples.extend([reset, after_reset]);
    }
    test
}

fn reference_server_choice(
    servers: &[(u32, u64, i32)],
    starting_bitrate_kbps: u32,
) -> Option<(usize, u32, u32, i32)> {
    let minimum_target = (u64::from(starting_bitrate_kbps) * 75 / 100) as u32;
    let mut best = None;
    for (index, &(measured, dropped, connect_time_ms)) in servers.iter().enumerate() {
        if connect_time_ms < 0 {
            continue;
        }
        let ideal = if dropped > 0 || measured < minimum_target {
            (u64::from(measured) * 70 / 100) as u32
        } else {
            starting_bitrate_kbps
        };
        let candidate = (index, measured, ideal, connect_time_ms);
        let replace = best.is_none_or(|current: (usize, u32, u32, i32)| {
            let close = ideal.abs_diff(current.2) < 400;
            (!close && ideal > current.2) || (close && connect_time_ms < current.3)
        });
        if replace {
            best = Some(candidate);
        }
    }
    best
}

#[test]
fn wizard_server_capacity_thresholds_are_exact() {
    assert_eq!(SERVER_MINIMUM_TARGET_PERCENT, 75);
    assert_eq!(SERVER_CAPACITY_PERCENT, 70);
    let below = choose_server(&[server_test("below", 749, 0, 20)], 1000).expect("server");
    assert_eq!(below.ideal_bitrate_kbps, 524);
    let exact = choose_server(&[server_test("exact", 750, 0, 20)], 1000).expect("server");
    assert_eq!(exact.ideal_bitrate_kbps, 1000);
    let with_drop = choose_server(&[server_test("drop", 1000, 1, 20)], 1000).expect("server");
    assert_eq!(with_drop.ideal_bitrate_kbps, 700);
}

#[test]
fn wizard_server_threshold_uses_integer_truncation_for_non_multiple_targets() {
    for (target, measured) in [(1001, 750), (1003, 752)] {
        let choice = choose_server(&[server_test("boundary", measured, 0, 20)], target)
            .expect("server selected");
        assert_eq!(choice.measured_bitrate_kbps, measured);
        assert_eq!(choice.ideal_bitrate_kbps, target);
    }
}

#[test]
fn wizard_server_penalty_includes_drops_at_the_trace_baseline() {
    let mut test = server_test("warmup-drop", 5000, 0, 20);
    test.samples[0].output_frames_dropped = 1;
    test.samples[1].output_frames_dropped = 1;
    let rollup = roll_up_window(&test.samples).expect("valid rollup");
    assert_eq!(rollup.output_frames_dropped, 0);
    let choice = choose_server(&[test], 5000).expect("server");
    assert_eq!(choice.measured_bitrate_kbps, 5000);
    assert_eq!(choice.ideal_bitrate_kbps, 3500);
}

#[test]
fn wizard_server_percentages_are_exact_for_large_valid_rates() {
    let choice =
        choose_server(&[server_test("large", 60_000_000, 0, 20)], 100_000_000).expect("server");
    assert_eq!(choice.measured_bitrate_kbps, 60_000_000);
    assert_eq!(choice.ideal_bitrate_kbps, 42_000_000);
}

#[test]
fn wizard_server_400_kbps_boundary_is_strict() {
    assert_eq!(SERVER_BITRATE_TIE_KBPS, 400);
    let higher = server_test("higher", 1000, 0, 100);
    let exactly_400_lower = server_test("lower", 858, 1, 1);
    let choice = choose_server(&[higher, exactly_400_lower], 1000).expect("server");
    assert_eq!(choice.name, "higher");

    let higher = server_test("higher", 1000, 0, 100);
    let three_ninety_nine_lower = server_test("lower", 859, 1, 1);
    let choice = choose_server(&[higher, three_ninety_nine_lower], 1000).expect("server");
    assert_eq!(choice.name, "lower");
}

#[test]
fn wizard_server_equal_connection_times_preserve_scan_order() {
    let first = server_test("first", 1000, 0, 20);
    let second = server_test("second", 900, 0, 20);
    let choice = choose_server(&[first, second], 1000).expect("server selected");
    assert_eq!(choice.name, "first");
}

fn candidate(encoder: &str, width: u32) -> Candidate {
    Candidate {
        encoder: encoder.into(),
        width,
        height: 720,
        fps_num: 60,
        fps_den: 1,
    }
}

#[test]
fn candidate_matrix_visits_every_encoder_at_each_mode_in_stable_order() {
    let encoders = vec!["obs_x264".to_string(), "obs_nvenc_h264".to_string()];
    let modes = vec![
        CandidateMode {
            width: 1920,
            height: 1080,
            fps_num: 60,
            fps_den: 1,
        },
        CandidateMode {
            width: 1280,
            height: 720,
            fps_num: 30,
            fps_den: 1,
        },
    ];

    let candidates = expand_candidate_matrix(&encoders, &modes);

    assert_eq!(
        candidates,
        vec![
            Candidate {
                encoder: "obs_x264".into(),
                width: 1920,
                height: 1080,
                fps_num: 60,
                fps_den: 1,
            },
            Candidate {
                encoder: "obs_x264".into(),
                width: 1280,
                height: 720,
                fps_num: 30,
                fps_den: 1,
            },
            Candidate {
                encoder: "obs_nvenc_h264".into(),
                width: 1920,
                height: 1080,
                fps_num: 60,
                fps_den: 1,
            },
            Candidate {
                encoder: "obs_nvenc_h264".into(),
                width: 1280,
                height: 720,
                fps_num: 30,
                fps_den: 1,
            },
        ]
    );
}

#[test]
fn bakeoff_records_failed_candidate_and_continues_in_order() {
    let mut planner = BakeoffPlanner::new(
        vec![
            candidate("x264", 1280),
            candidate("videotoolbox", 1920),
            candidate("vaapi", 1920),
        ],
        10_000,
    )
    .expect("valid plan");
    assert_eq!(planner.plan()[0].order, 1);
    assert_eq!(planner.plan()[0].candidate.encoder, "x264");
    assert_eq!(planner.plan()[1].candidate.encoder, "videotoolbox");
    assert_eq!(planner.plan()[2].candidate.encoder, "vaapi");
    assert!(
        planner
            .plan()
            .iter()
            .all(|planned| planned.duration_ms == 10_000)
    );

    assert_eq!(
        planner
            .next_candidate()
            .expect("next")
            .expect("candidate")
            .candidate
            .encoder,
        "x264"
    );
    assert_eq!(planner.complete_candidate(Verdict::Marginal), Ok(()));
    assert_eq!(
        planner
            .next_candidate()
            .expect("next")
            .expect("candidate")
            .candidate
            .encoder,
        "videotoolbox"
    );
    assert_eq!(planner.complete_candidate(Verdict::Fail), Ok(()));
    assert_eq!(planner.completed()[1].candidate.encoder, "videotoolbox");
    assert_eq!(planner.completed()[1].verdict, Verdict::Fail);
    assert_eq!(
        planner
            .next_candidate()
            .expect("next")
            .expect("candidate")
            .candidate
            .encoder,
        "vaapi"
    );
    assert_eq!(planner.complete_candidate(Verdict::Pass), Ok(()));
    assert_eq!(planner.next_candidate(), Ok(None));
    assert_eq!(planner.completed().len(), 3);
}

#[test]
fn bakeoff_rejects_invalid_plans_and_running_state() {
    assert_eq!(
        BakeoffPlanner::new(Vec::new(), 1000).err(),
        Some(PlannerError::EmptyCandidates)
    );
    assert_eq!(
        BakeoffPlanner::new(vec![candidate("x264", 1280)], 0).err(),
        Some(PlannerError::InvalidDuration)
    );
    assert_eq!(
        BakeoffPlanner::new(vec![candidate("", 1280)], 1000).err(),
        Some(PlannerError::InvalidCandidate)
    );
    let mut planner = BakeoffPlanner::new(vec![candidate("x264", 1280)], 1000).expect("plan");
    assert!(planner.next_candidate().expect("next").is_some());
    assert_eq!(
        planner.next_candidate(),
        Err(PlannerError::CandidateAlreadyRunning)
    );
    assert_eq!(planner.complete_candidate(Verdict::Pass), Ok(()));
    assert_eq!(
        planner.complete_candidate(Verdict::Pass),
        Err(PlannerError::NoCandidateRunning)
    );
}

#[test]
fn trace_validation_rejects_bad_time_and_counter_resets_start_new_windows() {
    let one = parse_samples(CLEAN)[0].clone();
    assert_eq!(
        roll_up_window(std::slice::from_ref(&one)),
        Err(AnalysisError::TooFewSamples)
    );
    let mut duplicate = one.clone();
    duplicate.timestamp_ms = 0;
    assert_eq!(
        roll_up_window(&[one.clone(), duplicate]),
        Err(AnalysisError::NonIncreasingTimestamp { sample_index: 1 })
    );
    let mut baseline = one.clone();
    baseline.timestamp_ms = 0;
    baseline.lagged_frames = 9;
    baseline.output_total_bytes = 10;
    baseline.output_total_frames = 10;
    baseline.output_frames_dropped = 1;
    baseline.encoder_total_frames = 10;
    baseline.encoder_skipped_frames = 1;
    let mut before_reset = baseline.clone();
    before_reset.timestamp_ms = 1000;
    before_reset.lagged_frames = 10;
    before_reset.output_total_bytes = 20;
    before_reset.output_total_frames = 20;
    before_reset.output_frames_dropped = 2;
    before_reset.encoder_total_frames = 20;
    before_reset.encoder_skipped_frames = 2;
    let mut reset = before_reset.clone();
    reset.timestamp_ms = 2000;
    reset.lagged_frames = 0;
    reset.output_total_bytes = 0;
    reset.output_total_frames = 0;
    reset.output_frames_dropped = 0;
    reset.encoder_total_frames = 0;
    reset.encoder_skipped_frames = 0;
    let mut after_reset = reset.clone();
    after_reset.timestamp_ms = 3000;
    after_reset.lagged_frames = 0;
    after_reset.output_total_bytes = 5;
    after_reset.output_total_frames = 5;
    after_reset.encoder_total_frames = 5;

    assert_eq!(
        roll_up_window(&[baseline.clone(), before_reset.clone(), reset.clone()]),
        Err(AnalysisError::TooFewSamples),
        "a reset with no later interval must not return a stale prior window"
    );
    let windows = roll_up_windows(&[baseline, before_reset, reset, after_reset], 5000)
        .expect("counter reset starts a new window");
    assert_eq!(windows.len(), 2);
    assert_eq!((windows[0].start_ms, windows[0].end_ms), (0, 1000));
    assert_eq!((windows[1].start_ms, windows[1].end_ms), (2000, 3000));
    assert_eq!(windows[1].output_bytes, 5);
    assert_eq!(windows[1].lagged_frames, 0);
    assert_eq!(
        judge_window(windows[1].clone()).verdict,
        Verdict::Pass,
        "a reset must not create a wrapping lag spike"
    );
}

#[test]
fn latest_window_and_server_validate_invalid_history_before_resets() {
    for kind in 0..4 {
        let (samples, expected) = invalid_prefix_after_reset(kind);
        assert_eq!(
            roll_up_window(&samples),
            Err(expected.clone()),
            "single-window input kind {kind}"
        );

        let server = ServerTest {
            name: format!("invalid-{kind}"),
            address: "rtmp://invalid".into(),
            samples,
        };
        assert_eq!(
            choose_server(&[server], 5000),
            Err(expected),
            "server input kind {kind}"
        );
    }
}

#[test]
fn every_decreasing_cumulative_counter_starts_a_new_window() {
    for counter in 0..6 {
        let mut baseline = parse_samples(CLEAN)[0].clone();
        baseline.timestamp_ms = 0;
        baseline.lagged_frames = 10;
        baseline.output_total_bytes = 10;
        baseline.output_total_frames = 10;
        baseline.output_frames_dropped = 10;
        baseline.encoder_total_frames = 10;
        baseline.encoder_skipped_frames = 10;
        let mut before_reset = baseline.clone();
        before_reset.timestamp_ms = 1000;
        before_reset.lagged_frames = 20;
        before_reset.output_total_bytes = 20;
        before_reset.output_total_frames = 20;
        before_reset.output_frames_dropped = 20;
        before_reset.encoder_total_frames = 20;
        before_reset.encoder_skipped_frames = 20;
        let mut reset = before_reset.clone();
        reset.timestamp_ms = 2000;
        match counter {
            0 => reset.lagged_frames = 0,
            1 => reset.output_total_bytes = 0,
            2 => reset.output_total_frames = 0,
            3 => reset.output_frames_dropped = 0,
            4 => reset.encoder_total_frames = 0,
            5 => reset.encoder_skipped_frames = 0,
            _ => unreachable!(),
        }
        let mut after_reset = reset.clone();
        after_reset.timestamp_ms = 3000;
        after_reset.lagged_frames += 1;
        after_reset.output_total_bytes += 1;
        after_reset.output_total_frames += 1;
        after_reset.output_frames_dropped += 1;
        after_reset.encoder_total_frames += 1;
        after_reset.encoder_skipped_frames += 1;

        let windows = roll_up_windows(&[baseline, before_reset, reset, after_reset], 5000)
            .expect("counter reset starts a new segment");
        assert_eq!(windows.len(), 2, "counter index {counter}");
        assert_eq!(windows[1].duration_ms, 1000, "counter index {counter}");
        assert_eq!(windows[1].output_bytes, 1, "counter index {counter}");
        assert_eq!(windows[1].lagged_frames, 1, "counter index {counter}");
    }
}

proptest! {
    #[test]
    fn rollup_uses_counter_deltas_from_nonzero_baselines(
        byte_base in 0u64..1_000_000,
        byte_delta in 0u64..1_000_000,
        frame_base in 0u64..100_000,
        frame_delta in 1u64..100_000,
        dropped_base in 0u64..100,
        dropped_delta in 0u64..100,
        encoder_base in 0u64..100_000,
        encoder_delta in 1u64..100_000,
        skipped_base in 0u64..100,
        skipped_delta in 0u64..100,
        duration_ms in 1u64..10_000,
    ) {
        let mut first = two_samples(8_000_000, 16_666_667, 0, 1, 0, 0.0, 1, 0, 50.0)
            .remove(0);
        first.output_total_bytes = byte_base;
        first.output_total_frames = frame_base;
        first.output_frames_dropped = dropped_base;
        first.encoder_total_frames = encoder_base;
        first.encoder_skipped_frames = skipped_base;
        let mut last = first.clone();
        last.timestamp_ms = duration_ms;
        last.output_total_bytes += byte_delta;
        last.output_total_frames += frame_delta;
        last.output_frames_dropped += dropped_delta;
        last.encoder_total_frames += encoder_delta;
        last.encoder_skipped_frames += skipped_delta;

        let rollup = roll_up_window(&[first, last]).expect("valid rollup");
        prop_assert_eq!(rollup.output_bytes, byte_delta);
        prop_assert!((rollup.output_bitrate_kbps - byte_delta as f64 * 8.0 / duration_ms as f64).abs() < 1e-9);
        prop_assert!((rollup.output_drop_percent - dropped_delta as f64 / frame_delta as f64 * 100.0).abs() < 1e-9);
        prop_assert!((rollup.encoder_skip_percent - skipped_delta as f64 / encoder_delta as f64 * 100.0).abs() < 1e-9);
    }

    #[test]
    fn server_capacity_matches_wizard_formula_for_u32_rates(
        starting_bitrate in any::<u32>(),
        measured_bitrate in any::<u32>(),
        initial_drops in 0u64..100,
        additional_drops in 0u64..2,
    ) {
        let mut test = server_test("property", measured_bitrate, 0, 20);
        test.samples[0].output_frames_dropped = initial_drops;
        test.samples[1].output_frames_dropped = initial_drops + additional_drops;
        let choice = choose_server(&[test], starting_bitrate).expect("server");
        let dropped = initial_drops > 0 || additional_drops > 0;
        // Match the wizard's integer expression order: target * 75 / 100.
        let minimum_target = u64::from(starting_bitrate) * 75 / 100;
        let low = u64::from(measured_bitrate) < minimum_target;
        let expected = if dropped || low {
            (u64::from(measured_bitrate) * 70 / 100) as u32
        } else {
            starting_bitrate
        };
        prop_assert_eq!(choice.measured_bitrate_kbps, measured_bitrate);
        prop_assert_eq!(choice.ideal_bitrate_kbps, expected);
    }

    #[test]
    fn server_capacity_threshold_property_biases_truncated_neighbors(
        starting_bitrate in any::<u32>(),
        threshold_side in -1i8..=1,
    ) {
        let minimum_target = (u64::from(starting_bitrate) * 75 / 100) as u32;
        let measured_bitrate = match threshold_side {
            -1 => minimum_target.saturating_sub(1),
            0 => minimum_target,
            _ => minimum_target.saturating_add(1),
        };
        let choice = choose_server(
            &[server_test("boundary-property", measured_bitrate, 0, 20)],
            starting_bitrate,
        ).expect("server");
        let expected = if measured_bitrate < minimum_target {
            (u64::from(measured_bitrate) * 70 / 100) as u32
        } else {
            starting_bitrate
        };
        prop_assert_eq!(choice.measured_bitrate_kbps, measured_bitrate);
        prop_assert_eq!(choice.ideal_bitrate_kbps, expected);
    }

    #[test]
    fn verdict_pressure_pairs_match_reference_bands_and_are_monotone(
        signal in 0u8..5,
        pair in 0u8..3,
    ) {
        // Ordered pairs cover Pass->Marginal, Marginal->Fail, and Pass->Fail.
        let (better_level, worse_level) = match pair {
            0 => (0, 1),
            1 => (1, 2),
            2 => (0, 2),
            _ => unreachable!(),
        };
        let expected = PRESSURE_BAND_EXPECTATIONS[signal as usize];
        let better = verdict_for(&coherent_samples_at_pressure(signal, better_level));
        let worse = verdict_for(&coherent_samples_at_pressure(signal, worse_level));

        prop_assert_eq!(
            better,
            expected[better_level as usize],
            "signal={}, level={}",
            signal,
            better_level
        );
        prop_assert_eq!(
            worse,
            expected[worse_level as usize],
            "signal={}, level={}",
            signal,
            worse_level
        );
        prop_assert!(
            verdict_rank(worse) >= verdict_rank(better),
            "pressure worsened for signal={signal}: {better:?} -> {worse:?}"
        );
    }

    #[test]
    fn invalid_prefix_after_reset_is_rejected_by_window_and_server_analysis(
        kind in 0u8..4,
    ) {
        let (samples, expected) = invalid_prefix_after_reset(kind);
        prop_assert_eq!(roll_up_window(&samples), Err(expected.clone()));
        let server = ServerTest {
            name: "invalid-history".into(),
            address: "rtmp://invalid".into(),
            samples,
        };
        prop_assert_eq!(choose_server(&[server], 5000), Err(expected));
    }

    #[test]
    fn multi_server_scan_matches_ordered_reference_across_counter_resets(
        starting_bitrate in any::<u32>(),
        cases in prop::collection::vec(
            (any::<u32>(), 0u64..2, -1i32..1000, any::<bool>()),
            2..8,
        ),
    ) {
        let first_connect_time = cases[0].2;
        let reference_inputs = cases.iter().enumerate().map(|(index, case)| {
            (case.0, case.1, if index == 1 { first_connect_time } else { case.2 })
        }).collect::<Vec<_>>();
        let tests = cases.iter().enumerate().map(|(index, case)| {
            let connect_time = if index == 1 { first_connect_time } else { case.2 };
            server_test_with_reset(
                &format!("server-{index}"), case.0, case.1, connect_time, case.3,
            )
        }).collect::<Vec<_>>();

        let expected = reference_server_choice(&reference_inputs, starting_bitrate);
        let actual = choose_server(&tests, starting_bitrate);
        match expected {
            Some((index, measured, ideal, connect_time)) => {
                let choice = actual.expect("reference scan found a successful server");
                prop_assert_eq!(choice.name, format!("server-{index}"));
                prop_assert_eq!(choice.measured_bitrate_kbps, measured);
                prop_assert_eq!(choice.ideal_bitrate_kbps, ideal);
                prop_assert_eq!(choice.connect_time_ms, connect_time);
            }
            None => prop_assert_eq!(actual, Err(AnalysisError::NoSuccessfulServer)),
        }
    }
}
