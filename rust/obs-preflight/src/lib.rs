#![forbid(unsafe_code)]

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::fmt;

// Five seconds smooths short encoder and network bursts while keeping a live
// preflight decision responsive. Callers may choose a different window size.
pub const DEFAULT_WINDOW_DURATION_MS: u64 = 5_000;

// OBS reports CPU usage as a percentage and output congestion as a 0.0..=1.0
// fraction (docs/sphinx/reference-outputs.rst, obs_output_get_congestion).
const MAX_CPU_PERCENT: f64 = 100.0;
const MAX_OUTPUT_CONGESTION: f64 = 1.0;

// Provisional Preflight M0 policy, owner-approved 2026-10-13: keep at least one
// fifth of each interval available for render bursts; OBS defines no such band.
pub const MARGINAL_RENDER_HEADROOM_RATIO: f64 = 0.20;
// Negative render headroom means rendering took longer than the frame interval.
pub const FAIL_RENDER_HEADROOM_NS: i64 = 0;

// Provisional Preflight M0 policy, owner-approved 2026-10-13: any render lag
// or output drop is visible; one percent is sustained short-test failure.
pub const MARGINAL_RENDER_LAG_PERCENT: f64 = 0.0;
pub const FAIL_RENDER_LAG_PERCENT: f64 = 1.0;
pub const MARGINAL_OUTPUT_DROP_PERCENT: f64 = 0.0;
pub const FAIL_OUTPUT_DROP_PERCENT: f64 = 1.0;

// OBSBasicStatusBar.cpp:474-491 computes skipped / total * 100, then warns at
// >= 0.1 and only after >10 new skipped frames. Thus 0.1 is 0.1%, not 10%.
pub const MARGINAL_ENCODER_SKIP_PERCENT: f64 = 0.1;
pub const MIN_ENCODER_SKIPS_FOR_UI_WARNING: u64 = 10;
// Provisional Preflight M0 policy, owner-approved 2026-10-13: one percent is a
// sustained fail band above OBS's existing warning; OBS sets no fail threshold.
pub const FAIL_ENCODER_SKIP_PERCENT: f64 = 1.0;

// Provisional Preflight M0 policy, owner-approved 2026-10-13: below 5% is
// ordinary variation; 20% means a material share of output is held back. OBS
// exposes congestion but defines no verdict bands.
pub const MARGINAL_CONGESTION_PERCENT: f64 = 5.0;
pub const FAIL_CONGESTION_PERCENT: f64 = 20.0;

// Provisional Preflight M0 policy, owner-approved 2026-10-13: below 90% leaves
// room for scene and OS bursts; at 98% little spare capacity remains. OBS does
// not define a preflight CPU warning threshold.
pub const MARGINAL_CPU_PERCENT: f64 = 90.0;
pub const FAIL_CPU_PERCENT: f64 = 98.0;

// Preserve AutoConfigTestPage.cpp:393-397: below 75% of target or any output
// drops triggers a 70% safety estimate from the measured bitrate.
pub const SERVER_MINIMUM_TARGET_PERCENT: u32 = 75;
pub const SERVER_CAPACITY_PERCENT: u32 = 70;
// Preserve AutoConfigTestPage.cpp:414-416: rates less than 400 kbps apart use
// the lower obs_output_get_connect_time_ms value as the tie-breaker.
pub const SERVER_BITRATE_TIE_KBPS: u32 = 400;

/// One timestamped set of counters sampled from the running OBS pipeline.
/// Counts are cumulative within one run; any decrease starts a fresh window
/// and the interval crossing the reset is omitted. Frame times are nanoseconds.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct StatSample {
    pub timestamp_ms: u64,
    pub average_frame_time_ns: u64,
    pub frame_interval_ns: u64,
    pub lagged_frames: u32,
    pub output_total_bytes: u64,
    pub output_total_frames: u64,
    pub output_frames_dropped: u64,
    /// OBS congestion is a fraction from 0.0 (none) to 1.0 (fully congested).
    pub output_congestion: f64,
    pub connect_time_ms: i32,
    pub encoder_total_frames: u64,
    pub encoder_skipped_frames: u64,
    /// CPU usage as a percentage from 0.0 to 100.0.
    pub cpu_percent: f64,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct WindowRollup {
    pub start_ms: u64,
    pub end_ms: u64,
    pub duration_ms: u64,
    pub output_bitrate_kbps: f64,
    pub output_bytes: u64,
    pub output_frames: u64,
    pub output_frames_dropped: u64,
    pub output_drop_percent: f64,
    pub encoder_frames: u64,
    pub encoder_frames_skipped: u64,
    pub encoder_skip_percent: f64,
    pub lagged_frames: u64,
    pub render_lag_percent: f64,
    pub average_frame_time_ns: f64,
    pub frame_interval_ns: f64,
    pub render_headroom_ns: i64,
    pub congestion_percent: f64,
    pub connect_time_ms: i32,
    pub cpu_percent: f64,
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Verdict {
    Pass,
    Marginal,
    Fail,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct WindowResult {
    pub rollup: WindowRollup,
    pub verdict: Verdict,
    pub reason: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum AnalysisError {
    TooFewSamples,
    ZeroWindowDuration,
    InvalidSample {
        sample_index: usize,
        field: &'static str,
    },
    NonIncreasingTimestamp {
        sample_index: usize,
    },
    CounterDecreased {
        sample_index: usize,
        field: &'static str,
    },
    NoSuccessfulServer,
}

impl fmt::Display for AnalysisError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::TooFewSamples => write!(f, "a window needs at least two samples"),
            Self::ZeroWindowDuration => write!(f, "window duration must be greater than zero"),
            Self::InvalidSample {
                sample_index,
                field,
            } => {
                write!(f, "sample {sample_index} has an invalid {field}")
            }
            Self::NonIncreasingTimestamp { sample_index } => write!(
                f,
                "sample {sample_index} timestamp must be later than the previous sample"
            ),
            Self::CounterDecreased {
                sample_index,
                field,
            } => write!(
                f,
                "sample {sample_index} {field} counter decreased during the test"
            ),
            Self::NoSuccessfulServer => write!(f, "no server test connected successfully"),
        }
    }
}

impl std::error::Error for AnalysisError {}

#[derive(Default)]
struct WindowAccumulator {
    start_ms: Option<u64>,
    end_ms: u64,
    duration_ms: u64,
    output_bytes: u64,
    output_frames: u64,
    output_frames_dropped: u64,
    encoder_frames: u64,
    encoder_frames_skipped: u64,
    lagged_frames: u64,
    expected_render_frames: f64,
    weighted_frame_time_ns: f64,
    weighted_frame_interval_ns: f64,
    weighted_headroom_ns: f64,
    weighted_congestion_percent: f64,
    weighted_cpu_percent: f64,
    connect_time_ms: i32,
}

impl WindowAccumulator {
    fn add_interval(
        &mut self,
        previous: &StatSample,
        current: &StatSample,
        sample_index: usize,
    ) -> Result<(), AnalysisError> {
        let duration_ms = current.timestamp_ms - previous.timestamp_ms;
        let duration = duration_ms as f64;
        let output_bytes = counter_delta(
            current.output_total_bytes,
            previous.output_total_bytes,
            sample_index,
            "output_total_bytes",
        )?;
        let output_frames = counter_delta(
            current.output_total_frames,
            previous.output_total_frames,
            sample_index,
            "output_total_frames",
        )?;
        let output_frames_dropped = counter_delta(
            current.output_frames_dropped,
            previous.output_frames_dropped,
            sample_index,
            "output_frames_dropped",
        )?;
        let encoder_frames = counter_delta(
            current.encoder_total_frames,
            previous.encoder_total_frames,
            sample_index,
            "encoder_total_frames",
        )?;
        let encoder_frames_skipped = counter_delta(
            current.encoder_skipped_frames,
            previous.encoder_skipped_frames,
            sample_index,
            "encoder_skipped_frames",
        )?;

        if self.start_ms.is_none() {
            self.start_ms = Some(previous.timestamp_ms);
        }
        self.end_ms = current.timestamp_ms;
        self.duration_ms = self.duration_ms.saturating_add(duration_ms);
        self.output_bytes = self.output_bytes.saturating_add(output_bytes);
        self.output_frames = self.output_frames.saturating_add(output_frames);
        self.output_frames_dropped = self
            .output_frames_dropped
            .saturating_add(output_frames_dropped);
        self.encoder_frames = self.encoder_frames.saturating_add(encoder_frames);
        self.encoder_frames_skipped = self
            .encoder_frames_skipped
            .saturating_add(encoder_frames_skipped);
        self.lagged_frames = self
            .lagged_frames
            .saturating_add((current.lagged_frames - previous.lagged_frames) as u64);
        self.expected_render_frames +=
            (duration_ms as f64 * 1_000_000.0) / current.frame_interval_ns as f64;
        self.weighted_frame_time_ns += current.average_frame_time_ns as f64 * duration;
        self.weighted_frame_interval_ns += current.frame_interval_ns as f64 * duration;
        self.weighted_headroom_ns +=
            (current.frame_interval_ns as f64 - current.average_frame_time_ns as f64) * duration;
        self.weighted_congestion_percent += current.output_congestion * 100.0 * duration;
        self.weighted_cpu_percent += current.cpu_percent * duration;
        self.connect_time_ms = current.connect_time_ms;
        Ok(())
    }

    fn finish(self) -> WindowRollup {
        let duration = self.duration_ms as f64;
        let average_frame_time_ns = self.weighted_frame_time_ns / duration;
        let frame_interval_ns = self.weighted_frame_interval_ns / duration;
        WindowRollup {
            start_ms: self.start_ms.unwrap_or_default(),
            end_ms: self.end_ms,
            duration_ms: self.duration_ms,
            output_bitrate_kbps: self.output_bytes as f64 * 8.0 / duration,
            output_bytes: self.output_bytes,
            output_frames: self.output_frames,
            output_frames_dropped: self.output_frames_dropped,
            output_drop_percent: percentage(self.output_frames_dropped, self.output_frames),
            encoder_frames: self.encoder_frames,
            encoder_frames_skipped: self.encoder_frames_skipped,
            encoder_skip_percent: percentage(self.encoder_frames_skipped, self.encoder_frames),
            lagged_frames: self.lagged_frames,
            render_lag_percent: if self.expected_render_frames == 0.0 {
                0.0
            } else {
                self.lagged_frames as f64 / self.expected_render_frames * 100.0
            },
            average_frame_time_ns,
            frame_interval_ns,
            render_headroom_ns: (self.weighted_headroom_ns / duration).round() as i64,
            congestion_percent: self.weighted_congestion_percent / duration,
            connect_time_ms: self.connect_time_ms,
            cpu_percent: self.weighted_cpu_percent / duration,
        }
    }
}

fn counter_delta(
    current: u64,
    previous: u64,
    sample_index: usize,
    field: &'static str,
) -> Result<u64, AnalysisError> {
    current
        .checked_sub(previous)
        .ok_or(AnalysisError::CounterDecreased {
            sample_index,
            field,
        })
}

fn percentage(numerator: u64, denominator: u64) -> f64 {
    if denominator == 0 {
        if numerator == 0 { 0.0 } else { 100.0 }
    } else {
        numerator as f64 / denominator as f64 * 100.0
    }
}

fn validate_sample(sample: &StatSample, sample_index: usize) -> Result<(), AnalysisError> {
    if sample.frame_interval_ns == 0 {
        return Err(AnalysisError::InvalidSample {
            sample_index,
            field: "frame_interval_ns",
        });
    }
    if !sample.output_congestion.is_finite()
        || !(0.0..=MAX_OUTPUT_CONGESTION).contains(&sample.output_congestion)
    {
        return Err(AnalysisError::InvalidSample {
            sample_index,
            field: "output_congestion",
        });
    }
    if !sample.cpu_percent.is_finite() || !(0.0..=MAX_CPU_PERCENT).contains(&sample.cpu_percent) {
        return Err(AnalysisError::InvalidSample {
            sample_index,
            field: "cpu_percent",
        });
    }
    Ok(())
}

fn counters_decreased(previous: &StatSample, current: &StatSample) -> bool {
    current.lagged_frames < previous.lagged_frames
        || current.output_total_bytes < previous.output_total_bytes
        || current.output_total_frames < previous.output_total_frames
        || current.output_frames_dropped < previous.output_frames_dropped
        || current.encoder_total_frames < previous.encoder_total_frames
        || current.encoder_skipped_frames < previous.encoder_skipped_frames
}

fn roll_up(
    samples: &[StatSample],
    window_duration_ms: Option<u64>,
) -> Result<Vec<WindowRollup>, AnalysisError> {
    if samples.len() < 2 {
        return Err(AnalysisError::TooFewSamples);
    }
    if window_duration_ms == Some(0) {
        return Err(AnalysisError::ZeroWindowDuration);
    }
    for (sample_index, sample) in samples.iter().enumerate() {
        validate_sample(sample, sample_index)?;
        if sample_index > 0 && sample.timestamp_ms <= samples[sample_index - 1].timestamp_ms {
            return Err(AnalysisError::NonIncreasingTimestamp { sample_index });
        }
    }

    let mut segment_index = 0_u64;
    let mut segment_start_ms = samples[0].timestamp_ms;
    let mut windows = BTreeMap::<(u64, u64), WindowAccumulator>::new();
    for (sample_index, pair) in samples.windows(2).enumerate() {
        let previous = &pair[0];
        let current = &pair[1];
        if counters_decreased(previous, current) {
            // A restarted output/video counter is a new run segment. The
            // crossing interval combines incomparable counters, so discard it.
            segment_index = segment_index.saturating_add(1);
            segment_start_ms = current.timestamp_ms;
            continue;
        }
        let window_index = match window_duration_ms {
            Some(window_duration_ms) => {
                (current.timestamp_ms - segment_start_ms - 1) / window_duration_ms
            }
            None => 0,
        };
        windows
            .entry((segment_index, window_index))
            .or_default()
            .add_interval(previous, current, sample_index + 1)?;
    }

    Ok(windows
        .into_values()
        .map(WindowAccumulator::finish)
        .collect())
}

/// Roll up the latest contiguous set of timestamped samples into one window.
/// When a counter resets, earlier completed segments are ignored here; use
/// [`roll_up_windows`] to receive a separate rollup for each segment.
pub fn roll_up_window(samples: &[StatSample]) -> Result<WindowRollup, AnalysisError> {
    if samples.len() < 2 {
        return Err(AnalysisError::TooFewSamples);
    }
    let latest_segment_start = samples
        .windows(2)
        .enumerate()
        .rev()
        .find(|(_, pair)| counters_decreased(&pair[0], &pair[1]))
        .map(|(index, _)| index + 1)
        .unwrap_or(0);
    let mut windows = roll_up(&samples[latest_segment_start..], None)?;
    windows.pop().ok_or(AnalysisError::TooFewSamples)
}

/// Split a trace into windows of the requested duration and roll up each one.
/// A sample interval crossing a boundary is assigned to the window containing
/// its ending sample; frequent sampling keeps that boundary assignment small.
/// Any decreasing cumulative counter starts a new segment and drops the
/// crossing interval; the segment's first completed interval begins a new window.
pub fn roll_up_windows(
    samples: &[StatSample],
    window_duration_ms: u64,
) -> Result<Vec<WindowRollup>, AnalysisError> {
    roll_up(samples, Some(window_duration_ms))
}

/// Apply the named M0 verdict limits to one completed window.
pub fn judge_window(rollup: WindowRollup) -> WindowResult {
    let mut verdict = Verdict::Pass;
    let mut reasons = Vec::new();

    if rollup.encoder_frames == 0 {
        promote(&mut verdict, Verdict::Fail);
        reasons.push("The encoder produced no frames during this window.".to_owned());
    }

    if rollup.render_headroom_ns < FAIL_RENDER_HEADROOM_NS {
        promote(&mut verdict, Verdict::Fail);
        reasons.push(format!(
            "Rendering exceeded the frame interval by {} ns.",
            rollup.render_headroom_ns.unsigned_abs()
        ));
    } else if rollup.frame_interval_ns > 0.0
        && rollup.render_headroom_ns as f64 / rollup.frame_interval_ns
            < MARGINAL_RENDER_HEADROOM_RATIO
    {
        promote(&mut verdict, Verdict::Marginal);
        reasons.push(format!(
            "Render headroom is below the {:.0}% safety margin.",
            MARGINAL_RENDER_HEADROOM_RATIO * 100.0
        ));
    }

    if rollup.render_lag_percent >= FAIL_RENDER_LAG_PERCENT {
        promote(&mut verdict, Verdict::Fail);
        reasons.push(format!(
            "Render lag reached {:.2}% of expected frames.",
            rollup.render_lag_percent
        ));
    } else if rollup.render_lag_percent > MARGINAL_RENDER_LAG_PERCENT {
        promote(&mut verdict, Verdict::Marginal);
        reasons.push("OBS reported lagged render frames.".to_owned());
    }

    if rollup.output_drop_percent >= FAIL_OUTPUT_DROP_PERCENT {
        promote(&mut verdict, Verdict::Fail);
        reasons.push(format!(
            "Output dropped {:.2}% of delivered frames.",
            rollup.output_drop_percent
        ));
    } else if rollup.output_drop_percent > MARGINAL_OUTPUT_DROP_PERCENT {
        promote(&mut verdict, Verdict::Marginal);
        reasons.push("The output dropped frames during this window.".to_owned());
    }

    if rollup.encoder_skip_percent >= FAIL_ENCODER_SKIP_PERCENT {
        promote(&mut verdict, Verdict::Fail);
        reasons.push(format!(
            "The encoder skipped {:.2}% of its frames.",
            rollup.encoder_skip_percent
        ));
    } else if rollup.encoder_frames_skipped > MIN_ENCODER_SKIPS_FOR_UI_WARNING
        && rollup.encoder_skip_percent >= MARGINAL_ENCODER_SKIP_PERCENT
    {
        promote(&mut verdict, Verdict::Marginal);
        reasons.push(format!(
            "The encoder skipped {:.2}% of its frames, above OBS's warning level.",
            rollup.encoder_skip_percent
        ));
    }

    if rollup.congestion_percent >= FAIL_CONGESTION_PERCENT {
        promote(&mut verdict, Verdict::Fail);
        reasons.push(format!(
            "Network congestion reached {:.2}%.",
            rollup.congestion_percent
        ));
    } else if rollup.congestion_percent > MARGINAL_CONGESTION_PERCENT {
        promote(&mut verdict, Verdict::Marginal);
        reasons.push(format!(
            "Network congestion exceeded the {:.0}% warning level.",
            MARGINAL_CONGESTION_PERCENT
        ));
    }

    if rollup.cpu_percent >= FAIL_CPU_PERCENT {
        promote(&mut verdict, Verdict::Fail);
        reasons.push(format!(
            "CPU use reached {:.1}%, leaving almost no spare capacity.",
            rollup.cpu_percent
        ));
    } else if rollup.cpu_percent >= MARGINAL_CPU_PERCENT {
        promote(&mut verdict, Verdict::Marginal);
        reasons.push(format!(
            "CPU use reached {:.1}%, leaving little room for bursts.",
            rollup.cpu_percent
        ));
    }

    if reasons.is_empty() {
        reasons
            .push("The run kept render headroom and frame loss within the target limits.".into());
    }

    WindowResult {
        rollup,
        verdict,
        reason: reasons.join(" "),
    }
}

fn promote(current: &mut Verdict, candidate: Verdict) {
    let severity = |verdict| match verdict {
        Verdict::Pass => 0,
        Verdict::Marginal => 1,
        Verdict::Fail => 2,
    };
    if severity(candidate) > severity(*current) {
        *current = candidate;
    }
}

/// Per-server trace captured during one isolated test. Its counters must not
/// include unrelated earlier output runs. Positive dropped-frame totals seen
/// anywhere in this test include warm-up drops and trigger the wizard penalty.
/// Failed connections use a negative final connect_time_ms and are skipped,
/// matching the wizard's success-only server list.
#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct ServerTest {
    pub name: String,
    pub address: String,
    pub samples: Vec<StatSample>,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct ServerChoice {
    pub name: String,
    pub address: String,
    pub measured_bitrate_kbps: u32,
    pub ideal_bitrate_kbps: u32,
    pub connect_time_ms: i32,
}

/// Select a server using the current Auto Configuration Wizard's bandwidth,
/// tie, and connect-time rules.
pub fn choose_server(
    tests: &[ServerTest],
    starting_bitrate_kbps: u32,
) -> Result<ServerChoice, AnalysisError> {
    let mut best: Option<ServerChoice> = None;

    for test in tests {
        if test
            .samples
            .last()
            .is_none_or(|sample| sample.connect_time_ms < 0)
        {
            continue;
        }
        let rollup = roll_up_window(&test.samples)?;
        let measured_bitrate_kbps = ((rollup.output_bytes as u128 * 8) / rollup.duration_ms as u128)
            .min(u32::MAX as u128) as u32;
        let minimum_target_bitrate_kbps =
            percent_of_u32(starting_bitrate_kbps, SERVER_MINIMUM_TARGET_PERCENT);
        let dropped_any_frames = test
            .samples
            .iter()
            .any(|sample| sample.output_frames_dropped > 0);
        let ideal_bitrate_kbps =
            if dropped_any_frames || measured_bitrate_kbps < minimum_target_bitrate_kbps {
                percent_of_u32(measured_bitrate_kbps, SERVER_CAPACITY_PERCENT)
            } else {
                starting_bitrate_kbps
            };
        let candidate = ServerChoice {
            name: test.name.clone(),
            address: test.address.clone(),
            measured_bitrate_kbps,
            ideal_bitrate_kbps,
            connect_time_ms: rollup.connect_time_ms,
        };

        let should_replace = match &best {
            None => true,
            Some(current) => {
                let close = candidate
                    .ideal_bitrate_kbps
                    .abs_diff(current.ideal_bitrate_kbps)
                    < SERVER_BITRATE_TIE_KBPS;
                (!close && candidate.ideal_bitrate_kbps > current.ideal_bitrate_kbps)
                    || (close && candidate.connect_time_ms < current.connect_time_ms)
            }
        };
        if should_replace {
            best = Some(candidate);
        }
    }

    best.ok_or(AnalysisError::NoSuccessfulServer)
}

fn percent_of_u32(value: u32, percent: u32) -> u32 {
    // The product fits u64, and a percentage <= 100 cannot exceed `value`.
    ((u64::from(value) * u64::from(percent)) / 100) as u32
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize)]
pub struct Candidate {
    pub encoder: String,
    pub width: u32,
    pub height: u32,
    pub fps_num: u32,
    pub fps_den: u32,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct CandidatePlan {
    /// One-based position, preserving the order supplied by the caller.
    pub order: usize,
    pub candidate: Candidate,
    pub duration_ms: u64,
    /// Lets the caller end this candidate's sample time early on fail; the
    /// planner still records the result and advances to the next candidate.
    pub stop_if_fail: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum PlannerError {
    EmptyCandidates,
    InvalidDuration,
    InvalidCandidate,
    CandidateAlreadyRunning,
    NoCandidateRunning,
}

impl fmt::Display for PlannerError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::EmptyCandidates => write!(f, "a bake-off needs at least one candidate"),
            Self::InvalidDuration => write!(f, "candidate duration must be greater than zero"),
            Self::InvalidCandidate => {
                write!(f, "candidate encoder, resolution, and fps must be valid")
            }
            Self::CandidateAlreadyRunning => write!(
                f,
                "complete the running candidate before requesting another"
            ),
            Self::NoCandidateRunning => write!(f, "there is no running candidate to complete"),
        }
    }
}

impl std::error::Error for PlannerError {}

/// Sequential bake-off planner. Input order is preserved and each completed
/// verdict is recorded. A fail ends that candidate's test, then the planner
/// continues so one unusable encoder cannot hide other candidates.
pub struct BakeoffPlanner {
    plan: Vec<CandidatePlan>,
    next_index: usize,
    active: bool,
    completed: Vec<CandidateOutcome>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct CandidateOutcome {
    pub order: usize,
    pub candidate: Candidate,
    pub verdict: Verdict,
}

impl BakeoffPlanner {
    pub fn new(candidates: Vec<Candidate>, duration_ms: u64) -> Result<Self, PlannerError> {
        if candidates.is_empty() {
            return Err(PlannerError::EmptyCandidates);
        }
        if duration_ms == 0 {
            return Err(PlannerError::InvalidDuration);
        }
        if candidates.iter().any(|candidate| {
            candidate.encoder.trim().is_empty()
                || candidate.width == 0
                || candidate.height == 0
                || candidate.fps_num == 0
                || candidate.fps_den == 0
        }) {
            return Err(PlannerError::InvalidCandidate);
        }

        let plan = candidates
            .into_iter()
            .enumerate()
            .map(|(index, candidate)| CandidatePlan {
                order: index + 1,
                candidate,
                duration_ms,
                stop_if_fail: true,
            })
            .collect();
        Ok(Self {
            plan,
            next_index: 0,
            active: false,
            completed: Vec::new(),
        })
    }

    pub fn plan(&self) -> &[CandidatePlan] {
        &self.plan
    }

    pub fn completed(&self) -> &[CandidateOutcome] {
        &self.completed
    }

    pub fn next_candidate(&mut self) -> Result<Option<CandidatePlan>, PlannerError> {
        if self.active {
            return Err(PlannerError::CandidateAlreadyRunning);
        }
        if self.next_index >= self.plan.len() {
            return Ok(None);
        }
        self.active = true;
        Ok(Some(self.plan[self.next_index].clone()))
    }

    pub fn complete_candidate(&mut self, verdict: Verdict) -> Result<(), PlannerError> {
        if !self.active {
            return Err(PlannerError::NoCandidateRunning);
        }
        let candidate = &self.plan[self.next_index];
        self.completed.push(CandidateOutcome {
            order: candidate.order,
            candidate: candidate.candidate.clone(),
            verdict,
        });
        self.active = false;
        self.next_index += 1;
        Ok(())
    }
}
