//! Frame-budget window (obs-rust/obs-studio#846): libobs reports one-second
//! frame-time averages, and a single slow second at startup must not turn
//! the light red. The light uses the median of a window of samples.

use obs_gpu_status::*;

const BUDGET: u64 = 33_333_333;
const MS: u64 = 1_000_000;

#[test]
fn too_few_samples_give_no_timings() {
    assert_eq!(FrameStats::from_samples(&[], BUDGET), None);
    assert_eq!(FrameStats::from_samples(&[5 * MS; 4], BUDGET), None);
    assert!(FrameStats::from_samples(&[5 * MS; MIN_FRAME_SAMPLES], BUDGET).is_some());
}

#[test]
fn no_interval_gives_no_timings() {
    assert_eq!(FrameStats::from_samples(&[5 * MS; 10], 0), None);
}

#[test]
fn median_of_odd_and_even_windows() {
    let odd = FrameStats::from_samples(&[9 * MS, MS, 5 * MS, 3 * MS, 7 * MS], BUDGET).unwrap();
    assert_eq!(odd.median_frame_ns, 5 * MS);
    assert_eq!(odd.interval_ns, BUDGET);
    let even =
        FrameStats::from_samples(&[MS, 2 * MS, 4 * MS, 8 * MS, 16 * MS, 32 * MS], BUDGET).unwrap();
    assert_eq!(even.median_frame_ns, 6 * MS);
}

#[test]
fn one_slow_startup_second_stays_green() {
    // The report: red for one refresh after launch, then green.
    let samples = [400 * MS, 3 * MS, 2 * MS, 2 * MS, 2 * MS, 2 * MS];
    let frames = FrameStats::from_samples(&samples, BUDGET);
    assert_eq!(
        classify(Some("NVIDIA GeForce RTX 3060/PCIe/SSE2"), &[], frames),
        State::Green
    );
}

#[test]
fn sustained_overrun_is_red() {
    let samples = [95 * MS, 90 * MS, 99 * MS, 30 * MS, 97 * MS];
    let frames = FrameStats::from_samples(&samples, BUDGET);
    assert_eq!(
        classify(Some("NVIDIA GeForce RTX 3060/PCIe/SSE2"), &[], frames),
        State::Red
    );
}

#[test]
fn median_empty_and_single() {
    assert_eq!(median(&[]), None);
    assert_eq!(median(&[7]), Some(7));
}

#[test]
fn median_odd_count_is_the_middle_value() {
    assert_eq!(median(&[9, 1, 5]), Some(5));
    assert_eq!(median(&[3, 3, 3, 1, 100]), Some(3));
}

#[test]
fn median_even_count_is_the_midpoint_rounded_down() {
    assert_eq!(median(&[1, 3]), Some(2));
    assert_eq!(median(&[1, 2]), Some(1));
    assert_eq!(median(&[4, 1, 8, 2]), Some(3));
}

#[test]
fn median_does_not_overflow() {
    assert_eq!(median(&[u64::MAX, u64::MAX]), Some(u64::MAX));
    assert_eq!(median(&[u64::MAX - 1, u64::MAX]), Some(u64::MAX - 1));
    assert_eq!(median(&[0, u64::MAX]), Some(u64::MAX / 2));
}

#[test]
fn median_ignores_input_order() {
    assert_eq!(median(&[50, 10, 40, 20, 30]), median(&[10, 20, 30, 40, 50]));
}
