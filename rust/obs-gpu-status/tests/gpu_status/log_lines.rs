//! The dialog quotes the log lines that explain the GPU state
//! (obs-rust/obs-studio#944).

use obs_gpu_status::report::Report;
use obs_gpu_status::*;

#[test]
fn graphics_lines_are_kept_and_others_dropped() {
    let mut log = LogCapture::default();
    for line in [
        "Loading up OpenGL on adapter Mesa llvmpipe (LLVM 21.1.8, 256 bits)",
        "Unable to open DRM render node.",
        "Audio monitoring device:",
        "eglInitialize failed: EGL_NOT_INITIALIZED",
        "Loading up Direct3D 11 on adapter Microsoft Basic Render Driver (0)",
        "[obs-browser]: Version 2.24.6",
        "Failed to initialize video:  Graphics module not found",
    ] {
        log.push(line);
    }
    assert_eq!(
        log.lines(),
        vec![
            "Loading up OpenGL on adapter Mesa llvmpipe (LLVM 21.1.8, 256 bits)",
            "Unable to open DRM render node.",
            "eglInitialize failed: EGL_NOT_INITIALIZED",
            "Loading up Direct3D 11 on adapter Microsoft Basic Render Driver (0)",
            "Failed to initialize video:  Graphics module not found",
        ]
    );
}

#[test]
fn capture_is_bounded_deduplicated_and_truncated() {
    let mut log = LogCapture::default();
    for _ in 0..3 {
        log.push("Unable to open DRM render node.");
    }
    assert_eq!(log.lines().len(), 1);
    for i in 0..20 {
        log.push(&format!("eglCreateContext failed {i}"));
    }
    assert_eq!(log.lines().len(), MAX_LOG_LINES);
    let mut long = LogCapture::default();
    long.push(&format!("EGL error {}", "x".repeat(1000)));
    assert_eq!(long.lines()[0].chars().count(), MAX_LOG_LINE_CHARS);
}

#[test]
fn report_carries_the_lines_into_json() {
    let report = Report::new(Some("Mesa llvmpipe (LLVM 21.1.8, 256 bits)"), vec![], None)
        .with_log_lines(vec!["Unable to open DRM render node.".into()]);
    assert_eq!(report.log_lines, vec!["Unable to open DRM render node."]);
    let j = report.to_json();
    assert!(
        j.contains("\"logLines\":[\"Unable to open DRM render node.\"]"),
        "{j}"
    );
    let empty = Report::new(Some("NVIDIA GeForce RTX 3060"), vec![], None).to_json();
    assert!(empty.contains("\"logLines\":[]"), "{empty}");
}

#[test]
fn the_lights_own_log_lines_are_not_quoted_back() {
    // Review of #963: GpuStatus::Init() logs the causes, which mention EGL and
    // the DRM render node, and do_log fed them straight back in.
    let mut log = LogCapture::default();
    log.push("==== GPU acceleration: YELLOW ====");
    log.push("  possible cause: OBS runs in a sandbox ... EGL/GLX libraries ...");
    log.push("  detected GPU: NVIDIA GeForce RTX 3060");
    log.push("GPU acceleration: green: Rendering on NVIDIA.");
    assert!(log.lines().is_empty(), "{:?}", log.lines());
}

#[test]
fn egl_matches_whole_words_only() {
    let mut log = LogCapture::default();
    log.push("Scene collection 'Negligible' loaded");
    log.push("Loaded plugin 'regleg-filter'");
    assert!(log.lines().is_empty(), "{:?}", log.lines());
    log.push("libEGL warning: egl: failed to create dri2 screen");
    log.push("eglInitialize failed: EGL_NOT_INITIALIZED");
    log.push("Failed to make context current: GLXBadDrawable");
    assert_eq!(log.lines().len(), 3, "{:?}", log.lines());
}

#[test]
fn the_latest_lines_win_when_full() {
    let mut log = LogCapture::default();
    for i in 0..25 {
        log.push(&format!("EGL error {i}"));
    }
    let lines = log.lines();
    assert_eq!(lines.len(), MAX_LOG_LINES);
    assert_eq!(lines.first().map(String::as_str), Some("EGL error 15"));
    assert_eq!(lines.last().map(String::as_str), Some("EGL error 24"));
}
