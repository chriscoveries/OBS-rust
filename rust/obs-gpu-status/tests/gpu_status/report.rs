//! Tier 1 tests for the dialog text and the `gpuAcceleration` JSON
//! (obs-rust/obs-studio#846).

use obs_gpu_status::report::Report;
use obs_gpu_status::*;

const LLVMPIPE: &str = "Mesa llvmpipe (LLVM 21.1.8, 256 bits)";

fn rtx3060() -> GpuDevice {
    GpuDevice {
        vendor: Vendor::Nvidia,
        device_id: Some("2504".into()),
        name: Some("NVIDIA GeForce RTX 3060".into()),
        render_node: false,
    }
}

#[test]
fn green_has_no_causes() {
    let r = Report::new(
        Some("NVIDIA GeForce RTX 3060/PCIe/SSE2"),
        vec![rtx3060()],
        None,
    );
    assert_eq!(r.state, State::Green);
    assert!(
        r.summary.contains("NVIDIA GeForce RTX 3060/PCIe/SSE2"),
        "{}",
        r.summary
    );
    assert!(r.causes.is_empty());
}

#[test]
fn red_names_the_unused_gpu_and_the_renderer() {
    let r = Report::new(Some(LLVMPIPE), vec![rtx3060()], None);
    assert_eq!(r.state, State::Red);
    assert!(
        r.summary
            .contains("Your GPU (NVIDIA GeForce RTX 3060) was detected"),
        "{}",
        r.summary
    );
    assert!(r.summary.contains("not using its driver"), "{}", r.summary);
    assert!(r.summary.contains(LLVMPIPE), "{}", r.summary);
    assert!(r.summary.contains("choppy"), "{}", r.summary);
    let causes = r.causes.join("\n");
    assert!(causes.contains("egl-wayland"), "{causes}");
    assert!(causes.contains("/dev/dri"), "{causes}");
    assert!(causes.contains("driver"), "{causes}");
}

#[test]
fn yellow_without_a_name_uses_the_vendor() {
    let d = GpuDevice {
        name: None,
        vendor: Vendor::Amd,
        ..rtx3060()
    };
    let r = Report::new(Some(LLVMPIPE), vec![d], None);
    assert!(
        r.summary.contains("Your GPU (AMD) was detected"),
        "{}",
        r.summary
    );
}

#[test]
fn red_cases_explain_themselves() {
    let r = Report::new(Some(LLVMPIPE), vec![], None);
    assert_eq!(
        r.state,
        State::Yellow,
        "no GPU at all: software rendering is expected"
    );
    assert!(r.summary.contains("No GPU was detected"), "{}", r.summary);

    let r = Report::new(None, vec![rtx3060()], None);
    assert_eq!(r.state, State::Red);
    assert!(
        r.summary.contains("Graphics initialization failed"),
        "{}",
        r.summary
    );

    let slow = FrameStats {
        median_frame_ns: 95_000_000,
        interval_ns: 33_333_333,
    };
    let r = Report::new(Some("NVIDIA GeForce RTX 3060"), vec![rtx3060()], Some(slow));
    assert_eq!(r.state, State::Red);
    assert!(r.summary.contains("95.0 ms"), "{}", r.summary);
    assert!(r.summary.contains("33.3 ms"), "{}", r.summary);
}

#[test]
fn json_shape() {
    let r = Report::new(Some("Mesa \"llvmpipe\" a\\b"), vec![rtx3060()], None);
    let j = r.to_json();
    assert!(
        j.starts_with("{\"state\":\"red\",\"adapter\":\"Mesa \\\"llvmpipe\\\" a\\\\b\","),
        "{j}"
    );
    assert!(
        j.contains(
            "\"detectedGpus\":[{\"vendor\":\"nvidia\",\"deviceId\":\"2504\",\"name\":\"NVIDIA GeForce RTX 3060\",\"renderNode\":false}]"
        ),
        "{j}"
    );
    assert!(j.contains("\"summary\":\""), "{j}");
    assert!(j.contains("\"causes\":[\""), "{j}");
    assert!(j.ends_with("]}"), "{j}");
}

#[test]
fn json_nulls_and_control_characters() {
    let d = GpuDevice {
        device_id: None,
        name: None,
        ..rtx3060()
    };
    let j = Report::new(None, vec![d], None).to_json();
    assert!(j.starts_with("{\"state\":\"red\",\"adapter\":null,"), "{j}");
    assert!(j.contains("\"deviceId\":null,\"name\":null"), "{j}");
    let j = Report::new(Some("a\nb\u{1}"), vec![], None).to_json();
    assert!(j.contains("\"adapter\":\"a\\nb\\u0001\""), "{j}");
}

#[test]
fn state_names() {
    assert_eq!(State::Green.as_str(), "green");
    assert_eq!(State::Yellow.as_str(), "yellow");
    assert_eq!(State::Red.as_str(), "red");
    assert_eq!(Vendor::Intel.as_str(), "intel");
}
