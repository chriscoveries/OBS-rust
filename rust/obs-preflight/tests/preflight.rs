use obs_preflight::{
    AnalysisError, BakeoffPlanner, Candidate, DEFAULT_WINDOW_DURATION_MS, FAIL_CONGESTION_PERCENT,
    FAIL_CPU_PERCENT, FAIL_ENCODER_SKIP_PERCENT, FAIL_OUTPUT_DROP_PERCENT, FAIL_RENDER_HEADROOM_NS,
    FAIL_RENDER_LAG_PERCENT, MARGINAL_CONGESTION_PERCENT, MARGINAL_CPU_PERCENT,
    MARGINAL_ENCODER_SKIP_PERCENT, MARGINAL_OUTPUT_DROP_PERCENT, MARGINAL_RENDER_HEADROOM_RATIO,
    MARGINAL_RENDER_LAG_PERCENT, MIN_ENCODER_SKIPS_FOR_UI_WARNING, PlannerError,
    SERVER_BITRATE_TIE_KBPS, SERVER_CAPACITY_PERCENT, SERVER_MINIMUM_TARGET_PERCENT, ServerTest,
    StatSample, Verdict, choose_server, judge_window, roll_up_window, roll_up_windows,
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
fn golden_congested_uplink_fails_with_exact_reason() {
    let result = judge_window(roll_up_window(&parse_samples(CONGESTED)).expect("valid trace"));
    assert_eq!(result.verdict, Verdict::Fail);
    assert_eq!(
        result.reason,
        "Output dropped 3.33% of delivered frames. Network congestion reached 35.00%."
    );
}

#[test]
fn golden_encoder_falling_behind_fails_with_exact_reason() {
    let result = judge_window(roll_up_window(&parse_samples(ENCODER_BEHIND)).expect("valid trace"));
    assert_eq!(result.verdict, Verdict::Fail);
    assert_eq!(result.reason, "The encoder skipped 5.00% of its frames.");
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
fn render_headroom_threshold_boundaries_are_exact() {
    let interval = 1000;
    let above_margin = two_samples(799, interval, 0, 1000, 0, 0.0, 1000, 0, 50.0);
    assert_eq!(MARGINAL_RENDER_HEADROOM_RATIO, 0.20);
    assert_eq!(verdict_for(&above_margin), Verdict::Pass);

    let exact_margin = two_samples(800, interval, 0, 1000, 0, 0.0, 1000, 0, 50.0);
    assert_eq!(verdict_for(&exact_margin), Verdict::Pass);

    let below_margin = two_samples(801, interval, 0, 1000, 0, 0.0, 1000, 0, 50.0);
    assert_eq!(verdict_for(&below_margin), Verdict::Marginal);

    let exact_zero = two_samples(1000, interval, 0, 1000, 0, 0.0, 1000, 0, 50.0);
    assert_eq!(verdict_for(&exact_zero), Verdict::Marginal);
    assert_eq!(FAIL_RENDER_HEADROOM_NS, 0);

    let above_fail = two_samples(999, interval, 0, 1000, 0, 0.0, 1000, 0, 50.0);
    assert_eq!(verdict_for(&above_fail), Verdict::Marginal);

    let negative = two_samples(1001, interval, 0, 1000, 0, 0.0, 1000, 0, 50.0);
    assert_eq!(verdict_for(&negative), Verdict::Fail);
}

#[test]
fn render_lag_threshold_boundaries_are_exact() {
    let no_lag = two_samples(50_000, 100_000, 0, 10_000, 0, 0.0, 10_000, 0, 50.0);
    assert_eq!(verdict_for(&no_lag), Verdict::Pass);

    let one_lag = two_samples(50_000, 100_000, 1, 10_000, 0, 0.0, 10_000, 0, 50.0);
    assert_eq!(MARGINAL_RENDER_LAG_PERCENT, 0.0);
    assert_eq!(verdict_for(&one_lag), Verdict::Marginal);

    let below_fail = two_samples(50_000, 100_000, 99, 10_000, 0, 0.0, 10_000, 0, 50.0);
    assert_eq!(verdict_for(&below_fail), Verdict::Marginal);

    let fail_boundary = two_samples(50_000, 100_000, 100, 10_000, 0, 0.0, 10_000, 0, 50.0);
    assert_eq!(FAIL_RENDER_LAG_PERCENT, 1.0);
    assert_eq!(verdict_for(&fail_boundary), Verdict::Fail);

    let above_fail = two_samples(50_000, 100_000, 101, 10_000, 0, 0.0, 10_000, 0, 50.0);
    assert_eq!(verdict_for(&above_fail), Verdict::Fail);
}

#[test]
fn output_drop_threshold_boundaries_are_exact() {
    let no_drops = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 1000, 0, 50.0);
    assert_eq!(verdict_for(&no_drops), Verdict::Pass);

    let marginal = two_samples(500_000, 1_000_000, 0, 10_000, 1, 0.0, 1000, 0, 50.0);
    assert_eq!(MARGINAL_OUTPUT_DROP_PERCENT, 0.0);
    assert_eq!(verdict_for(&marginal), Verdict::Marginal);

    let below_fail = two_samples(500_000, 1_000_000, 0, 10_000, 99, 0.0, 1000, 0, 50.0);
    assert_eq!(verdict_for(&below_fail), Verdict::Marginal);

    let fail_boundary = two_samples(500_000, 1_000_000, 0, 10_000, 100, 0.0, 1000, 0, 50.0);
    assert_eq!(FAIL_OUTPUT_DROP_PERCENT, 1.0);
    assert_eq!(verdict_for(&fail_boundary), Verdict::Fail);

    let above_fail = two_samples(500_000, 1_000_000, 0, 10_000, 101, 0.0, 1000, 0, 50.0);
    assert_eq!(verdict_for(&above_fail), Verdict::Fail);
}

#[test]
fn encoder_skip_ui_warning_boundaries_are_exact() {
    let below_percent = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 11_001, 11, 50.0);
    assert_eq!(verdict_for(&below_percent), Verdict::Pass);

    let count_gate_not_met = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 10_000, 10, 50.0);
    assert_eq!(verdict_for(&count_gate_not_met), Verdict::Pass);
    assert_eq!(MIN_ENCODER_SKIPS_FOR_UI_WARNING, 10);

    let exact_ui_warning = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 11_000, 11, 50.0);
    assert_eq!(MARGINAL_ENCODER_SKIP_PERCENT, 0.1);
    assert_eq!(verdict_for(&exact_ui_warning), Verdict::Marginal);

    let above_ui_warning = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 10_999, 11, 50.0);
    assert_eq!(verdict_for(&above_ui_warning), Verdict::Marginal);

    let below_fail = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 10_000, 99, 50.0);
    assert_eq!(verdict_for(&below_fail), Verdict::Marginal);

    let fail_boundary = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 10_000, 100, 50.0);
    assert_eq!(FAIL_ENCODER_SKIP_PERCENT, 1.0);
    assert_eq!(verdict_for(&fail_boundary), Verdict::Fail);

    let above_fail = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 10_000, 101, 50.0);
    assert_eq!(verdict_for(&above_fail), Verdict::Fail);
}

#[test]
fn congestion_threshold_boundaries_are_exact() {
    let below_marginal_edge = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0499, 1000, 0, 50.0);
    assert_eq!(verdict_for(&below_marginal_edge), Verdict::Pass);

    let exact_marginal_edge = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.05, 1000, 0, 50.0);
    assert_eq!(verdict_for(&exact_marginal_edge), Verdict::Pass);

    let above_marginal_edge = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0501, 1000, 0, 50.0);
    assert_eq!(MARGINAL_CONGESTION_PERCENT, 5.0);
    assert_eq!(verdict_for(&above_marginal_edge), Verdict::Marginal);

    let below_fail_edge = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.1999, 1000, 0, 50.0);
    assert_eq!(verdict_for(&below_fail_edge), Verdict::Marginal);

    let exact_fail_edge = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.20, 1000, 0, 50.0);
    assert_eq!(FAIL_CONGESTION_PERCENT, 20.0);
    assert_eq!(verdict_for(&exact_fail_edge), Verdict::Fail);

    let above_fail_edge = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.2001, 1000, 0, 50.0);
    assert_eq!(verdict_for(&above_fail_edge), Verdict::Fail);
}

#[test]
fn cpu_threshold_boundaries_are_exact() {
    let below_marginal = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 1000, 0, 89.99);
    assert_eq!(verdict_for(&below_marginal), Verdict::Pass);

    let exact_marginal = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 1000, 0, 90.0);
    assert_eq!(MARGINAL_CPU_PERCENT, 90.0);
    assert_eq!(verdict_for(&exact_marginal), Verdict::Marginal);

    let above_marginal = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 1000, 0, 90.01);
    assert_eq!(verdict_for(&above_marginal), Verdict::Marginal);

    let below_fail = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 1000, 0, 97.99);
    assert_eq!(verdict_for(&below_fail), Verdict::Marginal);

    let exact_fail = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 1000, 0, 98.0);
    assert_eq!(FAIL_CPU_PERCENT, 98.0);
    assert_eq!(verdict_for(&exact_fail), Verdict::Fail);

    let above_fail = two_samples(500_000, 1_000_000, 0, 1000, 0, 0.0, 1000, 0, 98.01);
    assert_eq!(verdict_for(&above_fail), Verdict::Fail);
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
        let low = u64::from(measured_bitrate) * 100 < u64::from(starting_bitrate) * 75;
        let expected = if dropped || low {
            (u64::from(measured_bitrate) * 70 / 100) as u32
        } else {
            starting_bitrate
        };
        prop_assert_eq!(choice.measured_bitrate_kbps, measured_bitrate);
        prop_assert_eq!(choice.ideal_bitrate_kbps, expected);
    }
}
