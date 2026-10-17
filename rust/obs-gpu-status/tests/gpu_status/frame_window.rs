//! The one-second frame-time window, moved out of the frontend so it is
//! tested (obs-rust/obs-studio#943).

use obs_gpu_status::report::Report;
use obs_gpu_status::*;

const BUDGET: u64 = 33_333_333;

#[test]
fn keeps_the_last_fifteen_samples() {
    let mut w = FrameWindow::default();
    for ns in 1..=20 {
        w.push(ns);
    }
    assert_eq!(w.samples(), (6..=20).collect::<Vec<u64>>());
    assert_eq!(FRAME_WINDOW, 15);
}

#[test]
fn zero_means_no_frame_yet_and_is_skipped() {
    let mut w = FrameWindow::default();
    w.push(0);
    w.push(7);
    w.push(0);
    assert_eq!(w.samples(), vec![7]);
}

#[test]
fn stats_need_min_frame_samples() {
    let mut w = FrameWindow::default();
    for _ in 0..MIN_FRAME_SAMPLES - 1 {
        w.push(2_000_000);
    }
    assert_eq!(w.stats(BUDGET), None);
    w.push(2_000_000);
    assert_eq!(
        w.stats(BUDGET),
        Some(FrameStats {
            median_frame_ns: 2_000_000,
            interval_ns: BUDGET
        })
    );
}

#[test]
fn bare_api_names_are_flagged() {
    for api in ["OpenGL", "Direct3D 11", "Metal"] {
        assert!(is_graphics_api_name(api), "{api}");
        let j = Report::new(Some(api), vec![], None).to_json();
        assert!(j.contains("\"adapterIsApiName\":true"), "{j}");
    }
    assert!(!is_graphics_api_name(
        "NVIDIA Corporation NVIDIA GeForce RTX 3060/PCIe/SSE2"
    ));
    let j = Report::new(Some("Mesa llvmpipe (LLVM 21.1.8, 256 bits)"), vec![], None).to_json();
    assert!(j.contains("\"adapterIsApiName\":false"), "{j}");
}
