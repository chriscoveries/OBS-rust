//! Tier 1 tests for obs-rust/obs-studio#846. This is new code with no C
//! original, so Tiers 2 and 3 do not apply.

use std::fs;
use std::path::{Path, PathBuf};

use obs_gpu_status::*;

fn nv() -> GpuDevice {
    GpuDevice {
        vendor: Vendor::Nvidia,
        device_id: Some("2504".into()),
        name: None,
        render_node: true,
    }
}

#[test]
fn hardware_adapters_are_green() {
    for a in [
        "NVIDIA Corporation NVIDIA GeForce RTX 3060/PCIe/SSE2",
        "AMD AMD Radeon RX 7900 XTX (radeonsi, navi31, LLVM 17.0.6, DRM 3.54)",
        "Intel Mesa Intel(R) UHD Graphics 630 (CFL GT2)",
        "NVIDIA GeForce RTX 2080 Ti",
    ] {
        assert!(!is_software_renderer(a), "{a}");
        assert_eq!(classify(Some(a), &[nv()], None), State::Green, "{a}");
        assert_eq!(classify(Some(a), &[], None), State::Green, "{a}");
    }
}

const SOFTWARE: [&str; 5] = [
    "Mesa llvmpipe (LLVM 21.1.8, 256 bits)",
    "Mesa softpipe",
    "Microsoft Basic Render Driver",
    "Google SwiftShader",
    "Mesa LLVMPIPE (LLVM 15.0.7, 128 bits)",
];

#[test]
fn software_with_gpu_present_is_yellow() {
    for vendor in [Vendor::Nvidia, Vendor::Amd, Vendor::Intel] {
        let d = GpuDevice { vendor, ..nv() };
        for a in SOFTWARE {
            assert!(is_software_renderer(a), "{a}");
            assert_eq!(
                classify(Some(a), std::slice::from_ref(&d), None),
                State::Yellow,
                "{a}"
            );
        }
    }
}

#[test]
fn software_without_gpu_is_red() {
    for a in SOFTWARE {
        assert_eq!(classify(Some(a), &[], None), State::Red, "{a}");
    }
}

#[test]
fn graphics_init_failure_is_red() {
    assert_eq!(classify(None, &[nv()], None), State::Red);
    assert_eq!(classify(None, &[], None), State::Red);
}

#[test]
fn frame_budget_overrun_is_red() {
    let hw = "NVIDIA GeForce RTX 3060";
    let interval = 33_333_333;
    let over = FrameStats {
        median_frame_ns: 95_000_000,
        interval_ns: interval,
    };
    let at = FrameStats {
        median_frame_ns: interval,
        interval_ns: interval,
    };
    let fast = FrameStats {
        median_frame_ns: 2_000_000,
        interval_ns: interval,
    };
    assert_eq!(classify(Some(hw), &[nv()], Some(over)), State::Red);
    assert_eq!(classify(Some(SOFTWARE[0]), &[nv()], Some(over)), State::Red);
    assert_eq!(classify(Some(hw), &[nv()], Some(at)), State::Green);
    assert_eq!(classify(Some(hw), &[nv()], Some(fast)), State::Green);
    assert_eq!(
        classify(Some(SOFTWARE[0]), &[nv()], Some(fast)),
        State::Yellow
    );
}

#[test]
fn pci_vendor_ids() {
    assert_eq!(Vendor::from_pci_id("0x10de\n"), Some(Vendor::Nvidia));
    assert_eq!(Vendor::from_pci_id("10DE"), Some(Vendor::Nvidia));
    assert_eq!(Vendor::from_pci_id("0x1002"), Some(Vendor::Amd));
    assert_eq!(Vendor::from_pci_id("0x8086"), Some(Vendor::Intel));
    assert_eq!(Vendor::from_pci_id("0x1af4"), None); // virtio
    assert_eq!(Vendor::from_pci_id(""), None);
}

struct Tree(PathBuf);

impl Drop for Tree {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

fn tree(name: &str) -> Tree {
    let p = std::env::temp_dir().join(format!("obs-gpu-status-{name}-{}", std::process::id()));
    let _ = fs::remove_dir_all(&p);
    fs::create_dir_all(&p).unwrap();
    Tree(p)
}

fn card(root: &Path, card: &str, vendor: &str, device: &str) {
    let d = root.join("sys/class/drm").join(card).join("device");
    fs::create_dir_all(&d).unwrap();
    fs::write(d.join("vendor"), format!("{vendor}\n")).unwrap();
    fs::write(d.join("device"), format!("{device}\n")).unwrap();
}

#[test]
fn detects_cards_from_sysfs_fixture() {
    let t = tree("sysfs");
    card(&t.0, "card0", "0x10de", "0x2504");
    card(&t.0, "card1", "0x10de", "0x1e07");
    card(&t.0, "card2", "0x1af4", "0x1050"); // virtio-gpu: not a known GPU
    card(&t.0, "renderD128", "0x10de", "0x2504");
    fs::create_dir_all(t.0.join("sys/class/drm/card0-HDMI-A-1")).unwrap(); // a connector
    fs::create_dir_all(t.0.join("dev/dri")).unwrap();
    fs::write(t.0.join("dev/dri/renderD128"), "").unwrap();

    let mut got = detect_devices(&t.0);
    got.sort_by(|a, b| a.device_id.cmp(&b.device_id));
    assert_eq!(got.len(), 2, "{got:?}");
    assert_eq!(got[0].device_id.as_deref(), Some("1e07"));
    assert!(!got[0].render_node);
    assert_eq!(got[1].device_id.as_deref(), Some("2504"));
    assert!(got[1].render_node);
    assert!(got.iter().all(|d| d.vendor == Vendor::Nvidia));
}

#[test]
fn render_node_needs_dev_dri() {
    // The sandbox case from #846: sysfs shows the render node, /dev/dri does not.
    let t = tree("nodev");
    card(&t.0, "card0", "0x10de", "0x2504");
    card(&t.0, "renderD128", "0x10de", "0x2504");
    let got = detect_devices(&t.0);
    assert_eq!(got.len(), 1);
    assert!(!got[0].render_node);
}

#[test]
fn missing_sysfs_detects_nothing() {
    let t = tree("empty");
    assert!(detect_devices(&t.0).is_empty());
}

#[test]
fn parses_nvidia_smi_output() {
    let out =
        "NVIDIA GeForce RTX 3060, 0x250410DE\nNVIDIA GeForce RTX 2080 Ti, 0x1E0710DE\n\ngarbage\n";
    let got = parse_nvidia_smi(out);
    assert_eq!(got.len(), 2);
    assert_eq!(got[0].name.as_deref(), Some("NVIDIA GeForce RTX 3060"));
    assert_eq!(got[0].device_id.as_deref(), Some("2504"));
    assert_eq!(got[1].device_id.as_deref(), Some("1e07"));
    assert!(
        got.iter()
            .all(|d| d.vendor == Vendor::Nvidia && !d.render_node)
    );
    assert!(
        parse_nvidia_smi(
            "NVIDIA-SMI has failed because it couldn't communicate with the NVIDIA driver."
        )
        .is_empty()
    );
}

#[test]
fn merge_fills_names_and_adds_missing() {
    let mut d = vec![nv()];
    merge_devices(
        &mut d,
        parse_nvidia_smi(
            "NVIDIA GeForce RTX 3060, 0x250410DE\nNVIDIA GeForce RTX 2080 Ti, 0x1E0710DE",
        ),
    );
    assert_eq!(d.len(), 2);
    assert_eq!(d[0].name.as_deref(), Some("NVIDIA GeForce RTX 3060"));
    assert!(d[0].render_node);
    assert_eq!(d[1].device_id.as_deref(), Some("1e07"));
}

#[test]
fn system_probe_does_not_panic() {
    let _ = platform::system_devices();
}
